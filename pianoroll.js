
const container = document.getElementById("pianoroll");
const canvas = document.getElementById("grid");
const ctx = canvas.getContext("2d");

// Grid settings
let gridWidth = 5000; // Total width (time axis)
let gridHeight = 2000; // Total height (note axis)
let cellWidth = 50; // Time step width
let cellHeight = 20; // Note height

// Set canvas size
canvas.width = gridWidth;
canvas.height = gridHeight;

// Draw the MIDI grid
function drawGrid() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = "#444"; // Grid line color
    ctx.lineWidth = 1;

    // Draw vertical lines (time axis)
    for (let x = 0; x < gridWidth; x += cellWidth) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, gridHeight);
        ctx.stroke();
    }

    // Draw horizontal lines (note axis)
    for (let y = 0; y < gridHeight; y += cellHeight) {
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(gridWidth, y);
        ctx.stroke();
    }
}

// Initial draw
drawGrid();


container.scrollTop = (canvas.height - container.clientHeight) / 2;