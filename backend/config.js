module.exports = function( ) {

	var config = {
		wunderground: {
			api_key: "423ab18ed43f2535",
			service_type: "developer", // developer, drizzle, shower, downpour
			ttl: 300, // minimum seconds between refreshes
			location: "/q/zmw:00000.1.06240", // /q/zmw:00000.1.06240 // http://autocomplete.wunderground.com/aq?query=amsterda
			autoip: false,
			formula: "Math.round(Math.sqrt(Math.min(({qpf}+0.05) * {pop}, 100)/100) * 255).toString(16)" // qpf = Quantitative Precipitation Forecast (0-Xmm), pop = Probability of Precipitation (0-100)
		},
		socket: {
			port: 9000
		}
	};

	return config;

};