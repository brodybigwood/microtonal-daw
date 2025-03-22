
container = document.getElementById("pianoroll");
canvas = document.getElementById("grid");
ctx = canvas.getContext("2d");
notesCanvas = document.getElementById("notes");
notesCtx = notesCanvas.getContext("2d");
playheadCanvas = document.getElementById("playhead")
playhead = playheadCanvas.getContext("2d")
topLayer = document.getElementById("topLayer")

xscale = 1
yscale = 1

measure = 0
lastX = 0

// Grid settings
let gridWidth = 5000; // Total width (time axis)
let gridHeight = 2000; // Total height (note axis)
let cellWidth = 50; // Time step width
let defaultNoteHeight = 20; // Note height


function setTemp(temp){
    cellHeight = defaultNoteHeight * 12/temp
    drawGrid()
}

setTemp(12)

// Set canvas size
canvas.width = gridWidth;
canvas.height = gridHeight;
notesCanvas.width = gridWidth;
notesCanvas.height = gridHeight;
playheadCanvas.height = gridHeight
playheadCanvas.width = gridWidth
topLayer.height = gridHeight
topLayer.width = gridWidth

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

draggingPlayhead = false

// Click to Add Note
topLayer.addEventListener("mousedown", (event) => {
    if (event.button === 0) {

        let playHeadDiff = Math.abs(x-measure)
        if(playHeadDiff < 5) {

            draggingPlayhead = true

          
           
        } else {
            

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
            note = { 
                x: snapX, 
                y: snapY, 
                width: cellWidth * 2, 
                end: snapX+(cellWidth*2),
                height: cellHeight, 
                color: "blue" ,
                isPlaying: false
            };
            notes.push(note);

            // Redraw Notes
            drawNotes(note);
        }

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
        notesCtx.fillRect(note.x+1, note.y+1, note.width-2, note.height-2);
    }

}


   // Handle right-click (contextmenu) to delete note
   topLayer.addEventListener("contextmenu", function(e) {
    e.preventDefault(); // Prevent default context menu


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
    console.log(notes);
    notes.splice(i, 1);
    console.log(notes); 

    // Clear the deleted note's area on the canvas
    notesCtx.clearRect(note.x, note.y, note.width, note.height);
}

let isDragging = false;
let draggingNote = null;
let edge = ""; // "left" or "right" to track which edge is being dragged

topLayer.addEventListener("mousemove", function(e) {
    currentX = e.clientX
    dX = currentX - lastX
    rect = notesCanvas.getBoundingClientRect();
    x = e.clientX - rect.left;
    y = e.clientY - rect.top;
    x /= xscale
    y /= yscale

    let hoveredNote = null;
    let hoverPlayhead = false;

    let playHeadDiff = Math.abs(x-measure)

    if(draggingPlayhead) {
            cursorstyle = "ew-resize";
            measure += dX
            renderPlayhead()
    }
    if(playHeadDiff < 5 || draggingPlayhead) {
        cursorstyle = "ew-resize";  // Right edge hover
    }
    else {
    cursorstyle = "default"


    // Loop through notes to detect if hovering over a left or right edge
    for (let i = 0; i < notes.length; i++) {
        note = notes[i];

        // Check if mouse is near the left or right edge of the note
        if (x >= note.x - 5 && x <= note.x + 5 && y >= note.y && y <= note.y + note.height) {
            hoveredNote = note;
            cursorstyle = "ew-resize";  // Left edge hover
        } else if (x >= note.x + note.width - 5 && x <= note.x + note.width + 5 && y >= note.y && y <= note.y + note.height) {
            hoveredNote = note;
            cursorstyle = "ew-resize";  // Right edge hover
        }
    }

    if (!hoveredNote) {
        // No note hovered, reset cursor to default
        cursorstyle= "default";
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
}
lastX = currentX;
topLayer.style.cursor = cursorstyle
});


topLayer.addEventListener("mouseup", function(e) {
    // End dragging
    isDragging = false;
    draggingNote = null;
    edge = "";
    draggingPlayhead = false
});


topLayer.addEventListener("wheel", function(e) {
    console.log(isCtrlPressed+" "+isShiftPressed)
    if(e.deltaY < 0) {
        direction = 1;
    } else {
        direction = -1;
    }
    if (isCtrlPressed && isShiftPressed) {
        e.preventDefault()
        zoom(3, e, direction)
        return
    }
    if (isCtrlPressed) {
        console.log('preventing zoom')
        e.preventDefault()
        zoom(1, e, direction)
    }
    if (isShiftPressed) {
        e.preventDefault()
        zoom(2, e, direction)
    }
});



function zoom(axis, e, direction) {

    let rect = notesCanvas.getBoundingClientRect();
    let mouseX = x / rect.width * 100;
    let mouseY = y / rect.height * 100;

    if(axis == 1){
       xscale *= Math.pow(1.1, direction)
       transformOrigin = `${mouseX}% center`;
    } else
    if(axis == 2){
        yscale *= Math.pow(1.1, direction)
        transformOrigin = `center ${mouseY}%`;
    } else
    if(axis == 3){
        xscale *= Math.pow(1.1, direction)
        yscale *= Math.pow(1.1, direction)
        transformOrigin = `${mouseX}% ${mouseY}%`;
    }
    
    scaleFactor = `scale(${xscale}, ${yscale})`


    

    notesCanvas.style.transformOrigin = transformOrigin;
    canvas.style.transformOrigin = transformOrigin;
    topLayer.style.transformOrigin = transformOrigin;

    notesCanvas.style.transform = scaleFactor
    canvas.style.transform = scaleFactor
    topLayer.style.transform = scaleFactor

    
}


play = false;
time = 0;
interval = false;
prevMeasure = 0

function renderPlayhead() {


 playhead.clearRect(prevMeasure-2, 0, 6, playheadCanvas.height);
 playhead.strokeStyle = "red"; // Grid line color
 playhead.lineWidth = 2;
 playhead.beginPath();
 playhead.moveTo(measure, 0);
 playhead.lineTo(measure, playheadCanvas.height);
 playhead.stroke();

 if(isAnimating) {
    for(let note of notes) {
        if(!note.isPlaying && note.x <= measure && note.end >= measure) {
            midiOn(note)
        }
        if(note.isPlaying && note.end <= measure && note.x <= measure) {
            midiOff(note)
        }
    }
}
prevMeasure = measure

}


let isAnimating = false;
let animationFrameId;


const frameDuration = 1000 / 10;  // Target frame rate (60 FPS)



masterTime = 0;

function animate(time) {

    

    if (!startTime) {
        startTime = Date.now();  // Define startTime only once the loop starts
    }
    let frameTime = Date.now() - startTime;  // Calculate elapsed time

    if (frameTime >= frameDuration) {  // If a second has passed
        measure += frameTime /100000 * playheadCanvas.width
        renderPlayhead();  // Convert to seconds
 
        startTime = Date.now();  // Reset the start time for the next second
    }

    if (isAnimating) {
        animationFrameId = requestAnimationFrame(animate);
    }
}



// Start the animation
function togglePlay() {
    if (!isAnimating) {
        isAnimating = true;
        startTime = Date.now();  // Initialize once outside the loop
        animationFrameId = requestAnimationFrame(animate);
    } else
    if (isAnimating) {
        isAnimating = false;
        cancelAnimationFrame(animationFrameId);
    }
}

function midiOn(note) {
    note.isPlaying = true
    console.log('note on at measure '+measure)
}

function midiOff(note) {
    note.isPlaying = false
    console.log('note off at measure '+measure)
}