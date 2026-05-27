/*
 * index.js — PebbleKit JS for the Initiative watchface.
 *
 * Fetches current weather from Open-Meteo (no API key required) using
 * the phone's geolocation and forwards the temperature to the watch
 * via AppMessage. Fires on launch and re-fetches every 30 minutes
 * while the watchface is active. The JS context dies when the
 * watchface is dismissed, so on next launch we fetch again.
 */

var WEATHER_REFRESH_MS = 30 * 60 * 1000;

function fetchWeather() {
  if (!navigator.geolocation) {
    console.log('geolocation unavailable');
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function onPos(pos) {
      var lat = pos.coords.latitude;
      var lon = pos.coords.longitude;
      var url = 'https://api.open-meteo.com/v1/forecast?latitude=' +
                lat + '&longitude=' + lon +
                '&current_weather=true&temperature_unit=celsius';
      var xhr = new XMLHttpRequest();
      xhr.open('GET', url, true);
      xhr.timeout = 10000;
      xhr.onload = function onLoad() {
        if (xhr.status < 200 || xhr.status >= 300) {
          console.log('weather http ' + xhr.status);
          return;
        }
        try {
          var data = JSON.parse(xhr.responseText);
          var temp = Math.round(data.current_weather.temperature);
          Pebble.sendAppMessage(
            { 'WEATHER_TEMP': temp },
            function onSent() { console.log('weather sent: ' + temp + 'C'); },
            function onFail(e) {
              console.log('weather send failed: ' + JSON.stringify(e));
            }
          );
        } catch (e) {
          console.log('weather parse error: ' + e);
        }
      };
      xhr.onerror = function onErr() { console.log('weather xhr error'); };
      xhr.ontimeout = function onTo() { console.log('weather xhr timeout'); };
      xhr.send();
    },
    function onErr(err) {
      console.log('geolocation error: ' + err.message);
    },
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener('ready', function onReady() {
  console.log('pkjs ready');
  fetchWeather();
  setInterval(fetchWeather, WEATHER_REFRESH_MS);
});
