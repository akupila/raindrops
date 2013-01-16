module.exports = function(app) {

	var http = require("http");
	var RequestCaching = require('node-request-caching');
	var rc = new RequestCaching();

	(function validate() {
		if (!/[\w\d]{16}/.test(app.config.wunderground.api_key)) {
			throw new Error("Wunderground API key (" + app.config.wunderground.api_key + ") is incorrect (in config.js). Go to http://www.wunderground.com/weather/api/ to get your API key");
		}

		// validate, format: /q/zmw:00000.1.06240
		if (!app.config.wunderground.autoip && !/\/q\/zmw:\d{5}\.\d\.\d{5}/.test(app.config.wunderground.location)) {
			throw new Error("Wunderground location (" + app.config.wunderground.location + ") is incorrect (in config.js). Format should be /q/zmw:12345.1.12345\nCheck http://autocomplete.wunderground.com/aq?query={location} to get the id");
		}

		var maxCalls = 0;
		switch (app.config.wunderground.service_type) {
			case "developer":
				maxCalls = 500;
				break;
			case "drizzle":
				maxCalls = 5000;
				break;
			case "shower":
				maxCalls = 100000;
				break;
			case "downpour":
				maxCalls = 1000000;
				break;
			default:
				throw new Error("Invalid service_type in config.js. service_type must be develoer, drizzle, shower or downpour. Verify your service type in http://www.wunderground.com/weather/api/d/" + app.config.wunderground.api_key + "/edit.html");
				break;
		}
		var minimumTTL = Math.ceil(24 * 60 * 60 / maxCalls);
		if (app.config.wunderground.ttl < minimumTTL) {
			app.config.wunderground.ttl = minimumTTL;
			console.warn("WARNING: TTL set lower to what's allowed by wunderground for service type '" + app.config.wunderground.service_type + "'. TTL has been raised to " + minimumTTL + ", if the service type is wrong update it in config.js");
		}
	})();

	function formatResponse(wundergroundJsonString) {
		var wundergroundJsonData = JSON.parse(wundergroundJsonString);
		var hourly = wundergroundJsonData.hourly_forecast;

		var results = [];
		var i = 0;
		var n = 12;
		for (var i = 0; i < Math.min(hourly.length, n); i++) {
			var hour = hourly[i];
			var qpf = parseFloat(hour.qpf.metric);
			if (isNaN(qpf)) qpf = 0;
			var pop = parseFloat(hour.pop);
			if (isNaN(pop)) pop = 0;
			var formula = app.config.wunderground.formula;
			formula = formula.replace("{qpf}", qpf);
			formula = formula.replace("{pop}", pop);
			var result = eval(formula);
			console.log(hour.FCTTIME.hour + ":" + hour.FCTTIME.min + ": " + formula + " = " + result);
			results.push(result >= 10 ? result : "0" + result);
		}
		while (results.length < 12) results.push(0);

		return "u:" + results.join("|");
	}

	return function(data, responder, client) {
		if (data != "reload") return false;

		var apiKey = app.config.wunderground.api_key;
		var location = app.config.wunderground.location;

		console.log("Reloading weather information");

		var t = new Date().getTime();

		var url = "http://api.wunderground.com/api/" + apiKey + "/hourly" + location + ".json";
		if (app.config.wunderground.autoip) url = "http://api.wunderground.com/api/" + apiKey + "/hourly/q/autoip.json"

		rc.get(
			url,
			null,
			app.config.wunderground.ttl,
			function(err, res, body, cache) {
				if (err) {
					console.error(err);
					responder("err");
				} else {
					console.log("Got data. Cached: " + cache.hit);
					responder(formatResponse(body));
				}
			}
		);

		return true;
	};

};