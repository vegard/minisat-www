
$(document).ready(function () {
	var data = [];
	var g = new Dygraph(document.getElementById('graph'));

	socket = new WebSocket('ws://localhost:8000', 'minisat');

	socket.onerror = function (e) {
		alert('WebSocket error: ' + e.data);
	};

	var x = 0;
	socket.onmessage = function (e) {
		var json = JSON.parse(e.data);
		if (json.action == 'restart') {
		} else if (json.action == 'play') {
			$('.button.play').addClass('disabled');
			$('.button.pause').removeClass('disabled');
			$('.button.step').addClass('disabled');
		} else if (json.action == 'pause') {
			$('.button.play').removeClass('disabled');
			$('.button.pause').addClass('disabled');
			$('.button.step').removeClass('disabled');
		} else if (json.action == 'step') {
			data.push([x++, json.data]);
			g.updateOptions({
				'file': data,
			});
		}
	};

	socket.onopen = function (e) {
	};

	$('.button.restart').click(function () {
		socket.send('restart');
	});

	$('.button.play').click(function () {
		socket.send('play');
	});

	$('.button.pause').click(function () {
		socket.send('pause');
	});

	$('.button.step').click(function () {
		socket.send('step');
	});
});
