const express = require("express");
const { WebSocketServer } = require("ws");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.static(path.join(__dirname, "public")));

const server = app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

const wss = new WebSocketServer({ server });
const clients = new Set();

wss.on("connection", (ws) => {
  clients.add(ws);

  ws.on("message", (data) => {
    const messageString = data.toString();

    clients.forEach((client) => {
      if (client.readyState === 1) {
        client.send(messageString);
      }
    });
  });

  ws.on("close", () => {
    clients.delete(ws);
  });
});

// Keep host connection active
setInterval(() => {
  clients.forEach((client) => {
    if (client.readyState === 1) {
      client.send(JSON.stringify({ type: "ping" }));
    }
  });
}, 30000);
