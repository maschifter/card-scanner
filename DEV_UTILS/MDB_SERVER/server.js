// Importowanie modułu Express
const express = require("express");
const path = require("path");

// Inicjalizacja aplikacji Express
const app = express();
const PORT = 3000;

app.use(express.static(path.join(__dirname, "public")));

app.get("/", (req, res) => {
  res.sendFile(path.join(__dirname, "public", "index.html"));
});

app.listen(PORT, () => {
  console.log(`File server is running on: http://localhost:${PORT}`);
  console.log(`Serves files from directory: ${path.join(__dirname, "public")}`);
});
