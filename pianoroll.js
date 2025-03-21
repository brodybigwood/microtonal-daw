
container = document.getElementById("pianoroll");
canvas = document.getElementById("grid");
ctx = canvas.getContext("2d");
notesCanvas = document.getElementById("notes");
notesCtx = notesCanvas.getContext("2d");

// Grid settings
let gridWidth = 5000; // Total width (time axis)
let gridHeight = 2000; // Total height (note axis)
let cellWidth = 50; // Time step width
let cellHeight = 20; // Note height

// Set canvas size
canvas.width = gridWidth;
canvas.height = gridHeight;
notesCanvas.width = gridWidth;
notesCanvas.height = gridHeight;

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

// Notes Data
notes = [];

// Click to Add Note
notesCanvas.addEventListener("mousedown", (event) => {
    if (event.button === 0) {

        let hoveredNote = null;
    
        // Loop through notes to detect if clicking on a left or right edge
        for (let i = 0; i < notes.length; i++) {
            note = notes[i];
    
            if (x >= note.x - 5 && x <= note.x + 5 && y >= note.y && y <= note.y + note.height) {
                hoveredNote = note;
                edge = "left";  // Start dragging the left edge
                isDragging = true;
                draggingNote = hoveredNote;
                break;
            } else if (x >= note.x + note.width - 5 && x <= note.x + note.width + 5 && y >= note.y && y <= note.y + note.height) {
                hoveredNote = note;
                edge = "right";  // Start dragging the right edge
                isDragging = true;
                draggingNote = hoveredNote;
                break;
            }
        }

        if(!hoveredNote) {
            // Snap to Grid
            snapX = Math.floor(x / cellWidth) * cellWidth;
            snapY = Math.floor(y / cellHeight) * cellHeight;

            // Create Note Object
            note = { x: snapX, y: snapY, width: cellWidth * 2, height: cellHeight, color: "blue" };
            notes.push(note);

            // Redraw Notes
            drawNotes(note);
        }


    }
});

// Draw Notes
function drawNotes(note, all=false) {
    if(all){
        notesCtx.clearRect(0, 0, notesCanvas.width, notesCanvas.height);
        for (note of notes) {
            notesCtx.fillStyle = note.color;
            notesCtx.fillRect(note.x, note.y, note.width, note.height);
        }
    } else {
        notesCtx.fillStyle = note.color;
        notesCtx.fillRect(note.x, note.y, note.width, note.height);
    }

}


   // Handle right-click (contextmenu) to delete note
   notesCanvas.addEventListener("contextmenu", function(e) {
    e.preventDefault(); // Prevent default context menu
    rect = notesCanvas.getBoundingClientRect();
    x = e.clientX - rect.left;
    y = e.clientY - rect.top;

    // Check if any note is clicked
    findAndDeleteNote(x, y);
});

// Function to check for note at a specific position and delete it
function findAndDeleteNote(x, y) {


    // Loop through notes array to find the one at the clicked position
    for (let i = 0; i < notes.length; i++) {
        note = notes[i];
        if (x >= note.x && x <= (note.x + note.width) && y >= note.y && y <= (note.y + note.height)) {
            // Found the note, remove it from the array
            deleteNote(note, i)

            break;
        }
    }
}
function deleteNote(note, i){
    console.log(notes);  // Check the contents of the array before and after deletion
    //console.log(i)
    notes.splice(i, 1);
    console.log(notes);  // Check the contents of the array before and after deletion

    // Clear the deleted note's area on the canvas
    notesCtx.clearRect(note.x, note.y, note.width, note.height);
}

let isDragging = false;
let draggingNote = null;
let edge = ""; // "left" or "right" to track which edge is being dragged

notesCanvas.addEventListener("mousemove", function(e) {
    rect = notesCanvas.getBoundingClientRect();
    x = e.clientX - rect.left;
    y = e.clientY - rect.top;

    let hoveredNote = null;

    // Loop through notes to detect if hovering over a left or right edge
    for (let i = 0; i < notes.length; i++) {
        note = notes[i];

        // Check if mouse is near the left or right edge of the note
        if (x >= note.x - 5 && x <= note.x + 5 && y >= note.y && y <= note.y + note.height) {
            hoveredNote = note;
            notesCanvas.style.cursor = "ew-resize";  // Left edge hover
        } else if (x >= note.x + note.width - 5 && x <= note.x + note.width + 5 && y >= note.y && y <= note.y + note.height) {
            hoveredNote = note;
            notesCanvas.style.cursor = "ew-resize";  // Right edge hover
        }
    }

    if (!hoveredNote) {
        // No note hovered, reset cursor to default
        notesCanvas.style.cursor = "default";
    }

    // If dragging, resize or move the note based on the edge
    if (isDragging && draggingNote) {
        if (edge === "left") {
            const newWidth = draggingNote.width + (draggingNote.x - x);
            draggingNote.x = x;
            draggingNote.width = newWidth;
        } else if (edge === "right") {
            draggingNote.width = x - draggingNote.x;
        }

        // Redraw the notes
        drawNotes(draggingNote); // Redraw all notes after update
    }
});

notesCanvas.addEventListener("mousedown", function(e) {

});

notesCanvas.addEventListener("mouseup", function(e) {
    // End dragging
    isDragging = false;
    draggingNote = null;
    edge = "";
});
