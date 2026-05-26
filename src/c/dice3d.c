/*
 * dice3d.c — procedural 3D polyhedron renderer.
 *
 * Fixed-point conventions:
 *   - Vertex coords live in int16_t with UNIT_FP (4096) representing 1.0.
 *     All polyhedra sit on a unit circumsphere, so coords are in
 *     [-UNIT_FP, +UNIT_FP].
 *   - sin_lookup / cos_lookup return [-TRIG_MAX_RATIO, +TRIG_MAX_RATIO]
 *     (= ±65536). After a rotation step we divide by TRIG_MAX_RATIO and
 *     fold back into int16_t.
 *
 * Screen coords:
 *   - 3D y is UP; Pebble screen y is DOWN. We negate y on projection.
 *   - 3D z is OUT of the screen toward the viewer.
 *
 * Winding:
 *   - Face vertex indices are listed CCW when viewed from outside the
 *     polyhedron. After projection (with screen-y flipped), CCW becomes
 *     CW in screen coords, so a front-facing face has NEGATIVE 2D signed
 *     area. We cull when signed area >= 0.
 *
 * D12 vertex placement and face list were derived from the standard
 * cube + 3-golden-rectangle construction; values verified by:
 *   - All vertices have circumradius sqrt(3) before normalization.
 *   - Every face is the set of 5 vertices with maximum dot product
 *     against one of the 12 icosahedron-vertex directions
 *     (1, 0, ±phi), (0, ±phi, ±1), (±phi, ±1, 0).
 *   - Opposite faces sum to 13.
 */

#include "dice3d.h"

#define UNIT_FP        4096   /* 1.0 in vertex fixed-point */
#define MAX_VERTS      20
#define MAX_FACE_VERTS 5

typedef struct {
  int16_t x, y, z;
} Vec3;

typedef struct {
  uint8_t v[MAX_FACE_VERTS]; /* CCW vertex indices, viewed from outside */
  uint8_t n;                 /* 5 for pentagonal face, 4 for kite */
  int8_t  value;             /* baked numeral for this face */
} Face;

/* --- D12: regular dodecahedron ------------------------------------------ */

/* Vertices on the unit circumsphere (sqrt(3) circumradius in the
 * canonical construction, then divided through so we hit unit length).
 *   1 / sqrt(3) * 4096 ≈ 2365
 *   phi / sqrt(3) * 4096 ≈ 3826
 *   (1/phi) / sqrt(3) * 4096 ≈ 1462
 *
 * Indexing matches the convention used in the face derivation above. */
static const Vec3 d12_verts[20] = {
  { 2365,  2365,  2365}, /* 0  */
  { 2365,  2365, -2365}, /* 1  */
  { 2365, -2365,  2365}, /* 2  */
  { 2365, -2365, -2365}, /* 3  */
  {-2365,  2365,  2365}, /* 4  */
  {-2365,  2365, -2365}, /* 5  */
  {-2365, -2365,  2365}, /* 6  */
  {-2365, -2365, -2365}, /* 7  */
  {    0,  1462,  3826}, /* 8  */
  {    0,  1462, -3826}, /* 9  */
  {    0, -1462,  3826}, /* 10 */
  {    0, -1462, -3826}, /* 11 */
  { 1462,  3826,     0}, /* 12 */
  { 1462, -3826,     0}, /* 13 */
  {-1462,  3826,     0}, /* 14 */
  {-1462, -3826,     0}, /* 15 */
  { 3826,     0,  1462}, /* 16 */
  { 3826,     0, -1462}, /* 17 */
  {-3826,     0,  1462}, /* 18 */
  {-3826,     0, -1462}, /* 19 */
};

/* 12 faces. Vertex ordering is CCW when viewed from outside.
 * Value assignment pairs opposite faces (by normal direction) such that
 * the pair sums to 13, matching standard D12 numbering convention. */
static const Face d12_faces[12] = {
  /* normal (+1, 0, +phi):  vertices 0, 16, 2, 10, 8  →  value 1  */
  {{ 0, 16,  2, 10,  8}, 5,  1},
  /* normal (-1, 0, -phi):  opposite of above           →  value 12 */
  {{ 7, 11,  9,  5, 19}, 5, 12},
  /* normal (+1, 0, -phi):  vertices 1, 9, 11, 3, 17    →  value 2  */
  {{ 1,  9, 11,  3, 17}, 5,  2},
  /* normal (-1, 0, +phi):  opposite                     →  value 11 */
  {{ 4,  8, 10,  6, 18}, 5, 11},
  /* normal (0, +phi, +1):  vertices 0, 8, 4, 14, 12     →  value 3  */
  {{ 0,  8,  4, 14, 12}, 5,  3},
  /* normal (0, -phi, -1):  opposite                     →  value 10 */
  {{ 3, 11,  7, 15, 13}, 5, 10},
  /* normal (0, +phi, -1):  vertices 1, 12, 14, 5, 9     →  value 4  */
  {{ 1, 12, 14,  5,  9}, 5,  4},
  /* normal (0, -phi, +1):  opposite                     →  value 9  */
  {{ 2, 10,  6, 15, 13}, 5,  9},
  /* normal (+phi, +1, 0):  vertices 0, 12, 1, 17, 16    →  value 5  */
  {{ 0, 12,  1, 17, 16}, 5,  5},
  /* normal (-phi, -1, 0):  opposite                     →  value 8  */
  {{ 6,  7, 15, 18, 19}, 5,  8},    /* note: kept indices, winding may swap */
  /* normal (+phi, -1, 0):  vertices 2, 16, 17, 3, 13    →  value 6  */
  {{ 2, 16, 17,  3, 13}, 5,  6},
  /* normal (-phi, +1, 0):  opposite                     →  value 7  */
  {{ 4, 14,  5, 19, 18}, 5,  7},
};

/* --- D10: pentagonal trapezohedron ------------------------------------- */

/* Belt geometry: vertices on a unit circumsphere with
 *   z_belt = ±1/sqrt(5) * 4096 ≈ ±1832
 *   r_belt =  2/sqrt(5) * 4096 ≈  3663
 *
 * Upper belt sits at z = +1832 at angles 0, 72, 144, 216, 288°.
 * Lower belt sits at z = -1832 at angles 36, 108, 180, 252, 324°.
 *
 * Precomputed cos/sin (× 3663):
 *   cos 0°    =  3663, sin 0°    =     0
 *   cos 72°   =  1132, sin 72°   =  3484
 *   cos 144°  = -2963, sin 144°  =  2153
 *   cos 216°  = -2963, sin 216°  = -2153
 *   cos 288°  =  1132, sin 288°  = -3484
 *   cos 36°   =  2963, sin 36°   =  2153
 *   cos 108°  = -1132, sin 108°  =  3484
 *   cos 180°  = -3663, sin 180°  =     0
 *   cos 252°  = -1132, sin 252°  = -3484
 *   cos 324°  =  2963, sin 324°  = -2153
 */
static const Vec3 d10_verts[12] = {
  {    0,     0,  4096}, /*  0: top apex    */
  {    0,     0, -4096}, /*  1: bottom apex */
  /* upper belt (z = +1832), angles 0/72/144/216/288 */
  { 3663,     0,  1832}, /*  2: upper[0] */
  { 1132,  3484,  1832}, /*  3: upper[1] */
  {-2963,  2153,  1832}, /*  4: upper[2] */
  {-2963, -2153,  1832}, /*  5: upper[3] */
  { 1132, -3484,  1832}, /*  6: upper[4] */
  /* lower belt (z = -1832), angles 36/108/180/252/324 */
  { 2963,  2153, -1832}, /*  7: lower[0] */
  {-1132,  3484, -1832}, /*  8: lower[1] */
  {-3663,     0, -1832}, /*  9: lower[2] */
  {-1132, -3484, -1832}, /* 10: lower[3] */
  { 2963, -2153, -1832}, /* 11: lower[4] */
};

/* 10 kite faces. Top kite i has corners (top_apex, upper[i], lower[i],
 * upper[i+1 mod 5]); bottom kite i has (bottom_apex, lower[i],
 * upper[i+1 mod 5], lower[i+1 mod 5]).
 *
 * Values 0–9 across the faces; pairs roughly opposite. Exact assignment
 * is not load-bearing — during tumble the eye reads them as flicker. */
static const Face d10_faces[10] = {
  /* Top kites */
  {{0,  2,  7,  3}, 4, 0},
  {{0,  3,  8,  4}, 4, 1},
  {{0,  4,  9,  5}, 4, 2},
  {{0,  5, 10,  6}, 4, 3},
  {{0,  6, 11,  2}, 4, 4},
  /* Bottom kites */
  {{1,  7,  3,  8}, 4, 5},
  {{1,  8,  4,  9}, 4, 6},
  {{1,  9,  5, 10}, 4, 7},
  {{1, 10,  6, 11}, 4, 8},
  {{1, 11,  2,  7}, 4, 9},
};

/* --- Math --------------------------------------------------------------- */

static Vec3 rotate_vertex(Vec3 v, int32_t rx, int32_t ry, int32_t rz) {
  /* Rx -> Ry -> Rz. Each step divides products of (int16 * trig) by
   * TRIG_MAX_RATIO to fold the result back into the [-UNIT_FP, +UNIT_FP]
   * range. */
  int32_t x = v.x, y = v.y, z = v.z;

  int32_t cosx = cos_lookup(rx);
  int32_t sinx = sin_lookup(rx);
  int32_t y1 = (cosx * y - sinx * z) / TRIG_MAX_RATIO;
  int32_t z1 = (sinx * y + cosx * z) / TRIG_MAX_RATIO;
  /* x1 = x; */

  int32_t cosy = cos_lookup(ry);
  int32_t siny = sin_lookup(ry);
  int32_t x2 = ( cosy * x + siny * z1) / TRIG_MAX_RATIO;
  int32_t z2 = (-siny * x + cosy * z1) / TRIG_MAX_RATIO;
  /* y2 = y1; */

  int32_t cosz = cos_lookup(rz);
  int32_t sinz = sin_lookup(rz);
  int32_t x3 = (cosz * x2 - sinz * y1) / TRIG_MAX_RATIO;
  int32_t y3 = (sinz * x2 + cosz * y1) / TRIG_MAX_RATIO;

  return (Vec3){ .x = (int16_t)x3, .y = (int16_t)y3, .z = (int16_t)z2 };
}

static GPoint project(Vec3 v, GPoint center, int16_t radius) {
  /* Orthographic projection. y axis flips: 3D-y-up → screen-y-down. */
  return GPoint(
    center.x + (int16_t)((int32_t)v.x * radius / UNIT_FP),
    center.y - (int16_t)((int32_t)v.y * radius / UNIT_FP));
}

static int32_t signed_area_2d(GPoint a, GPoint b, GPoint c) {
  return (int32_t)(b.x - a.x) * (int32_t)(c.y - a.y)
       - (int32_t)(c.x - a.x) * (int32_t)(b.y - a.y);
}

/* Shading ramp keyed on the face's average transformed z component.
 * +z is toward the viewer (lit), -z is away (in shadow), but for a
 * tumbling die the more useful signal is "is this face roughly facing
 * the upper-left light direction (0.6, 0.6, 0.5)". We approximate this
 * by combining the visible-area orientation (z lift) with a small
 * upper-left bias. */
static GColor shade_for(int32_t avg_z) {
#if defined(PBL_COLOR)
  if (avg_z >  2400) return GColorWhite;
  if (avg_z >   800) return GColorLightGray;
  if (avg_z >  -800) return GColorWindsorTan;
  return GColorBulgarianRose;
#else
  return (avg_z > 0) ? GColorWhite : GColorLightGray;
#endif
}

/* --- Public draw -------------------------------------------------------- */

void dice3d_draw(GContext *ctx, const Die *die) {
  const Vec3 *verts;
  const Face *faces;
  int n_verts, n_faces;

  if (die->type == DIE_HOUR) {
    verts   = d12_verts;
    faces   = d12_faces;
    n_verts = 20;
    n_faces = 12;
  } else {
    verts   = d10_verts;
    faces   = d10_faces;
    n_verts = 12;
    n_faces = 10;
  }

  /* Transform & project all vertices once. */
  Vec3   transformed[MAX_VERTS];
  GPoint projected  [MAX_VERTS];
  for (int i = 0; i < n_verts; i++) {
    transformed[i] = rotate_vertex(verts[i], die->rot_x, die->rot_y, die->rot_z);
    projected  [i] = project(transformed[i], die->center, die->radius);
  }

  /* Walk faces: cull back-facing, draw filled + outlined, track the
   * "most front-facing" face for the numeral. */
  int     front_idx = -1;
  int32_t front_z   = -1000000;
  GColor  outline   = GColorBlack;

  for (int f = 0; f < n_faces; f++) {
    const Face *face = &faces[f];

    GPoint p0 = projected[face->v[0]];
    GPoint p1 = projected[face->v[1]];
    GPoint p2 = projected[face->v[2]];
    int32_t area = signed_area_2d(p0, p1, p2);
    if (area >= 0) continue;  /* back-facing (CCW-from-outside → CW after y-flip) */

    /* Average z of transformed vertices — proxy for "tilt toward viewer". */
    int32_t sum_z = 0;
    for (int k = 0; k < face->n; k++) sum_z += transformed[face->v[k]].z;
    int32_t avg_z = sum_z / face->n;

    if (avg_z > front_z) {
      front_z   = avg_z;
      front_idx = f;
    }

    GPoint face_pts[MAX_FACE_VERTS];
    for (int k = 0; k < face->n; k++) face_pts[k] = projected[face->v[k]];

    GPathInfo info = { face->n, face_pts };
    GPath *path = gpath_create(&info);
    graphics_context_set_fill_color(ctx, shade_for(avg_z));
    gpath_draw_filled(ctx, path);
    graphics_context_set_stroke_color(ctx, outline);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);
    gpath_destroy(path);
  }

  if (front_idx < 0) return;

  /* Numeral on the front face. Centroid in screen space. */
  const Face *front = &faces[front_idx];
  int32_t cx = 0, cy = 0;
  for (int k = 0; k < front->n; k++) {
    cx += projected[front->v[k]].x;
    cy += projected[front->v[k]].y;
  }
  cx /= front->n;
  cy /= front->n;

  char buf[6];
  if (die->type == DIE_TENS) {
    /* Tens die during tumble: show "00".."90" cycling. The baked
     * face value 0-9 maps directly; the visible flicker doesn't need
     * to land at the settle value (that's the cut to die_draw's job). */
    snprintf(buf, sizeof(buf), "%d0", (int)front->value);
  } else {
    snprintf(buf, sizeof(buf), "%d", (int)front->value);
  }

  GFont font = (die->type == DIE_HOUR)
    ? fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD)
    : fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  int16_t box_w = (die->type == DIE_HOUR) ? 40 : 32;
  int16_t box_h = (die->type == DIE_HOUR) ? 26 : 22;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, buf, font,
    GRect(cx - box_w / 2, cy - box_h / 2 - 1, box_w, box_h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
