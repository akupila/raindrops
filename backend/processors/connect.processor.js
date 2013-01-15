module.exports = function(app) {

	return function(data, responder, client) {
		if (data != "conn") return false;

		responder("ok");

		return true;
	};

};