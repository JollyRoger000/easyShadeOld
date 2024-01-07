var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// Wait for the DOM to be ready
$(document).ready(function () {
	initWebSocket();

	$("#divSetting").toolbar({
		addBackBtn: true,
		backBtnText: "Назад",
	});

	$("#btnOpen").on("click", onOpen);
	$("#btnClose").on("click", onClose);
	$("#btnStop").on("click", onStop);
	$("#btnCalibrate").on("click", onCalibrate);
	$("#btnOk").on("click", addTimer);
	$("#btnCancel").on("click", cancelTimer);
	$("#btnAddTimer").on("click", onAddTimerBtn);
	$("#slider").on("slidestop", onSlider);
	$("#settings").on("pageshow", getTimers);

	// Delete current timer
	$("#currentTimersTable").on("click", "tr", function () {
		var id = $(this).find("td:first").text();
		var msg = {
			cmd: "deleteTimer",
			id: id,
		};
		console.log(msg);
		if (websocket.readyState == websocket.OPEN) {
			websocket.send(JSON.stringify(msg));
		}
	});
});

// Send message for open
function onOpen() {
	var msg = {
		cmd: "open",
	};
	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Send message for close
function onClose() {
	var msg = {
		cmd: "close",
	};
	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Send message for stop
function onStop() {
	var msg = {
		cmd: "stop",
	};
	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Send message for calibrate
function onCalibrate() {
	var msg = {
		cmd: "calibrate",
	};

	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Send message for set shade
function onSlider() {
	var shade = $(this).val();
	var msg = {
		cmd: "setShade",
		shade: shade,
	};
	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Send message for get current timers
function getTimers() {
	var msg = {
		cmd: "getTimers",
	};
	console.log(msg);
	if (websocket.readyState == websocket.OPEN) {
		websocket.send(JSON.stringify(msg));
	}
}

// Initialize a WebSocket connection.
function initWebSocket() {
	console.log("Trying to open a WebSocket connection…");
	websocket = new WebSocket(gateway);
	websocket.onopen = onSocketOpen;
	websocket.onclose = onSocketClose;
	websocket.onmessage = onReceiveMessage;
}

// Called when the connection is open.
function onSocketOpen() {
	console.log("Connection opened");
}

// Called when the connection is closed.
function onSocketClose() {
	console.log("Connection closed");
	setTimeout(initWebSocket, 2000);
}

// Called when a message is received from the server esp.
function onReceiveMessage(event) {
	var data = JSON.parse(event.data);
	console.log("esp message: " + event.data);

	// Read calibrate status
	if (data.calibrateStatus != null) {
		var calibrateStatus = data.calibrateStatus;
		if (calibrateStatus == "true") {
			console.log("Калибровка выполнена");
			$("#btnCalibrate").removeClass("blinking-btn");
			$("#labelCalibrate1").text("-длина шторы настроена");
			$("#labelCalibrate2").text("-длина шторы настроена");
			$("#footer1").css("background-color", "#a8ecb1");
			$("#footer2").css("background-color", "#a8ecb1");
		}

		if (calibrateStatus == "false") {
			console.log("Калибровка не выполнена");
			$("#btnCalibrate").removeClass("blinking-btn");
			$("#labelCalibrate1").text("-настройка шторы не выполнена");
			$("#labelCalibrate2").text("-настройка шторы не выполнена");
			$("#footer1").css("background-color", "#ecbbbb");
			$("#footer2").css("background-color", "#ecbbbb");
		}

		if (calibrateStatus == "progress") {
			$("#btnCalibrate").addClass("blinking-btn");
			console.log("Калибровка выполняется");

			$("#labelCalibrate1").text("-выполняется настройка длины");
			$("#labelCalibrate2").text("-выполняется настройка длины");
		}
	} else {
		//$("#footer1").addClass("red-footer");
		//$("#footer2").addClass("red-footer");
	}
	// Read shade lenght
	if (data.shadeLenght != null) var shadeLenght = data.shadeLenght;

	// Read current shade level and set slider value
	if (data.shade != null) {
		var shade = data.shade;

		$("#slider").val(shade);
		$("#slider").slider("refresh");
	}

	// Read current timers and append to table
	if (data.timers != null) {
		var timers = data.timers;
		var id = [];
		var hour = [];
		var min = [];
		var shade = [];

		// Append timers to table
		if (timers.length > 0) {
			// Clear table
			$("#currentTimersTable").empty();
			// Append table header
			$("#currentTimersTable").append(
				"<tr><th>ID</th><th>Время</th><th>Затемнение</th></tr>"
			);

			for (let i = 0; i < timers.length; i++) {
				// Get timer data from array
				if (timers[i][0] != null) id[i] = timers[i][0];
				if (timers[i][1] != null) hour[i] = timers[i][1];
				if (timers[i][2] != null) min[i] = timers[i][2];
				if (timers[i][3] != null) shade[i] = timers[i][3];

				if (
					id[i] != null &&
					hour[i] != null &&
					min[i] != null &&
					shade[i] != null
				) {
					// Append table body
					$("#currentTimersTable").append(
						"<tr><td>" +
							id[i] +
							"</td><td>" +
							hour[i] +
							":" +
							min[i] +
							"</td><td>" +
							shade[i] +
							"</td></tr>"
					);
				}
				console.log(
					"Timer: " +
						i +
						"  id: " +
						id[i] +
						" hour: " +
						hour[i] +
						" min: " +
						min[i]
				);
				$("#currentTimersTable").table("refresh");
			}
		} else {
			console.log("No timers");
			$("#currentTimersTable").empty();
		}
	}
}

// Open timer popup dialog
function onAddTimerBtn() {
	$("#setTimerPopup").popup("open");
}

// Send message to esp to add timer and close timer popup dialog
function addTimer() {
	var time = $("#currentTimer").val();
	var shade = $("#currentShade").val();
	var hour = time.split(":")[0];
	var min = time.split(":")[1];
	var id = new Date().getTime();
	$("#setTimerPopup").popup("close");

	if (hour != null && min != null && shade != null) {
		var msg = {
			cmd: "addTimer",
			timer: [id, hour, min, shade],
		};
		if (websocket.readyState == websocket.OPEN) {
			websocket.send(JSON.stringify(msg));
		}
		console.log(msg);
	} else {
		console.log("Error: timer data is null");
		alert("Неверное значение времени или затемнения");
	}
}

// Close timer popup dialog and do nothing
function cancelTimer() {
	$("#setTimerPopup").popup("close");
}
