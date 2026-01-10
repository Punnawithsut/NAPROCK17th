const express = require("express");
const app = express();
const cors = require("cors");
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

app.post('/getBoard', (req, res) => {
  const { boardSize } = req.body;
  const n = parseInt(boardSize);
  let numbers = [];
  for (let i = 0; i < n * n / 2; i++) {
    numbers.push(i);
    numbers.push(i);
  }
  for (let i = numbers.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [numbers[i], numbers[j]] = [numbers[j], numbers[i]];
  }
  const board = [];
  for (let i = 0; i < n; i++) {
    board.push(numbers.slice(i * n, (i + 1) * n));
  }
  res.json(
    {
      "success": true,
      "board": board
    }
  )
});

app.post("/submitBoard", (req, res) => {
  const { initBoard, endBoard, path } = req.body;
  res.json({ "success": true });
})

module.exports = app;
