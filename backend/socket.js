module.exports = function(app) {

	var net = require('net');
	var fs = require('fs');

	var clients = [];
	var buffer = "";
	var processors = [];

	var directory = __dirname+ '/processors';
	fs.readdirSync( directory ).forEach(function( file ) {
		if (file.match(/(processor.js)$/i)) {
			console.log("Registering processor: " + file);
			processors.push(require(directory + "/" + file)(app));
		}
	});

	var createSocket = function(port) {
		net.createServer(connectHandler).listen(port);
		console.log("Socket listening on port " + port);
	};

	function connectHandler(socket) {
		// Identify this client
		socket.name = socket.remoteAddress + ":" + socket.remotePort;
		socket.latestActivity = new Date().getTime();

		// Put this new client in the list
		clients.push(socket);
		console.log("Socket " + socket.name + " connected");

		reload();

		// Handle incoming messages from clients.
		socket.on('data', function (data) {
			buffer += String(data);

			socket.latestActivity = new Date().getTime();

			process();
		});

		socket.on('error', function(error) {
			console.error("ERROR: '" + error + "'' for socket: " + socket.name);
			removeClient(socket);
		});

		// Remove the client from the list when it leaves
		socket.on('end', function () {
			removeClient(socket);
		});
	}

	function process() {
		var i = buffer.indexOf("\r\n");
		if (i >= 0) {
			// Strip invalid/corrupt data
			var data = buffer.substring(0, i).replace(/([^\w\d:,\-\|<>])/g, "");

			console.log(new Date().toString() + " : Incoming: " + data);

			var match = false;
			var n = processors.length;
			for (var j = 0; j < n; j++) {
				if (processors[j](data, broadcast)) {
					match = true;
					break;
				}
			}

			if (!match) {
				console.error("No processor for '" + data + "'. Echoing command.");
				broadcast(data);
			}

			buffer = buffer.substr(i + 2); // - \r \n
			process();
		}
	}

	// Send a message to all clients
	function broadcast(message) {
		clients.forEach(function (client) {
			if (!client || client === null || typeof(client) == "null" || typeof(client) == "undefined") {
				removeClient(client);
			} else {
				try {
					client.write(message + "\r\n");
				} catch (e) {
					console.error(e);
					removeClient(client);
					broadcast(message);
					return;
				}
			}
		});
		// Log it to the server output too
		console.log(new Date().toString() + " : Outgoing: " + message);
	}

	function removeClient(client) {
		clients.splice(clients.indexOf(client), 1);
		client.end("timeout");
	}

	function reload() {
		buffer = "reload\r\n";
		process();
	}

	createSocket(app.config.socket.port);
	
	return {
		reload: reload
	}

}