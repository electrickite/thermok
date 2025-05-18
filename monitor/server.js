const express = require('express');
const ws = require('ws');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const port = 3000;
const serialPath = '/dev/ttyACM0';
const baud = 9600;

const app = express();
const serial = new SerialPort({
  path: serialPath,
  baudRate: baud,
});
const parser = serial.pipe(new ReadlineParser({ delimiter: '\n' }))

const wss = new ws.Server({ noServer: true });
wss.on('connection', websocket => {
  console.log('Websocket connection established. Listening...')

  websocket.on('close', ()=> {
    console.log('Websocket connection closed');
  });
});

app.use(express.static('public'))

const server = app.listen(port, () => {
  console.log(`Listening on http://localhost:${port}`);
});

server.on('upgrade', (req, socket, head) => {
  console.log('Upgrading request to websocket connection - url:', req.url);

  wss.handleUpgrade(req, socket, head, socket => {
    wss.emit('connection', socket, req);
  });
});

serial.on('open', () => {
  console.log('Serial port open');
});

serial.on('error', function(err) {
  console.log('Error: ', err.message)
});

parser.on('data', (data) => {
  wss.clients.forEach(client => {
    client.send(data);
  });
});
