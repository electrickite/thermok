function ctof(c) {
  return (c * 1.8) + 32;
}

const ctx = document.getElementById('chart');

const chart = new Chart(ctx, {
  type: 'line',
  data: {
    datasets: [{
      label: 'Temperature',
      data: [],
      borderWidth: 1
    }]
  },
  options: {
    scales: {
      x: {
        type: 'time'
      }
    }
  }
});

const socket = new WebSocket('ws://localhost:3000');

socket.onopen = (event) => {
  // Handle connection open
};

socket.onmessage = (event) => {
  if (event.data.startsWith('C')) {
    let temp = parseFloat(event.data.replace(/C /, ''));
    if (!isNaN(temp)) {
      let date = new Date();
      chart.data.datasets[0].data.push({
        y: (Math.round(ctof(temp) * 10) / 10).toFixed(1),
        x: date.toISOString()
      });
      chart.update();
    }
  }
};

socket.onclose = (event) => {
  // Handle connection close
};
