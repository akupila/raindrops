var app = {
};
app.config = require('./config')(app);

var socket = require("./socket")(app);