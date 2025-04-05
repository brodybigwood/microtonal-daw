
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
lastY = 0

isMoving = false;
let movingNote = null;

temperament = 12

barLength = 200
// Grid settings
let gridWidth = 5000; // Total width (time axis)
let gridHeight = 2000; // Total height (note axis)
let cellWidth;
barDivisor = 4;

lastWidth = 1/barDivisor

function changeCells(divisor=barDivisor) {
    lastWidth = Math.round(lastWidth*divisor) /barDivisor
    barDivisor = divisor
    cellWidth = barLength/barDivisor; // Time step width

    drawGrid()
}


defaultNoteHeight = gridHeight/128
a440_height = defaultNoteHeight * (128-70)


isDeleting = false;

selectedNotes = []

function select(noteIndex) {
    if(noteIndex == "all") {
        n = notes.length
        selectedNotes = Array.from({ length: n }, (_, index) => index);
        updateNotes(notes[0],true)
    } else {
        selectedNotes.push(noteIndex)
        updateNotes(notes[noteIndex])
    }
}

function deSelect(noteIndex) {
    if(noteIndex == "all") {
        selectedNotes = []
        updateNotes(notes[0],true)
    } else {
        selectedNotes.splice(noteIndex,1)
        updateNotes(notes[noteIndex])
    }
}

function setTemp(temp){
    cellHeight = defaultNoteHeight * 12/temp
    drawGrid()
    temperament = temp
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
    ctx.lineWidth = 1;
    ctx.strokeStyle = "#444"
    topLimit = ((a440_height/cellHeight)-Math.floor(a440_height/cellHeight)) * cellHeight;
    bottomLimit = Math.floor(gridHeight-a440_height) * cellHeight + topLimit

    // Draw vertical lines (time axis)
    for (let x = 0; x < gridWidth; x += cellWidth) {
        ctx.beginPath();
        ctx.moveTo(x, topLimit+cellHeight);
        ctx.lineTo(x, bottomLimit);
        ctx.stroke();
    }

    // Draw horizontal lines (note axis)
    let isLightColor = false; // variable to track the current stroke color

    for (let y = topLimit; y < bottomLimit; y += cellHeight) {
        if (y <= a440_height + 5 && y >= a440_height - 5) {
            let prevColor = ctx.strokeStyle;
            ctx.strokeStyle = "red";
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(gridWidth, y);
            ctx.stroke();
            ctx.strokeStyle = prevColor;
        } else {
            if (isLightColor) {
                ctx.strokeStyle = "#666";
            } else {
                ctx.strokeStyle = "#444";
            }
            isLightColor = !isLightColor; // toggle the color for the next iteration
    
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(gridWidth, y);
            ctx.stroke();
        }
    }
    
}

// Initial draw
changeCells()

playNoteOnTouch = false

container.scrollTop = (canvas.height - container.clientHeight) / 2;

// Notes Data
notes = [];

draggingPlayhead = false

// Click to Add Note
topLayer.addEventListener("mousedown", (event) => {
    if (event.button === 0) {
        event.preventDefault()

        let playHeadDiff = Math.abs(x-measure*barLength)
        if(playHeadDiff < 5) {
            console.log('dragging')

            draggingPlayhead = true

          
           
        } else {
            

        let hoveredNote = null;
        let movingNote = null;
    
        // Loop through notes to detect if clicking on a left or right edge
        for (let i = 0; i < notes.length; i++) {
            note = notes[i];




    
            if (x >= note.x*cellWidth*barDivisor - 5 && x <= note.x*cellWidth*barDivisor + 5 && y >= note.y*note.height*canvas.height && y <= note.y*note.height*canvas.height + note.height*canvas.height) {
                hoveredNote = note;
                edge = "left";  // Start dragging the left edge
                isDragging = true;
                draggingNote = hoveredNote;
                break;
            } else if (x >= note.x*cellWidth*barDivisor + note.width*barLength - 5 && x <= note.x*cellWidth*barDivisor + note.width*barLength + 5 && y >= note.y*note.height*canvas.height && y <= note.y*note.height*canvas.height + note.height*canvas.height) {
                hoveredNote = note;
                edge = "right";  // Start dragging the right edge
                isDragging = true;
                draggingNote = hoveredNote;
                break;
            } else
            if (x >= note.x*cellWidth*barDivisor && x <= (note.x*cellWidth*barDivisor + note.width*barLength) && y >= note.y*note.height*canvas.height && y <= (note.y*note.height*canvas.height + note.height*canvas.height)) {
                movingNote = note;
                isMoving = true;
                setTemp(note.temp)
                changeCells(note.divisor)
                if(isCtrlPressed) {
                    noteIndex = notes.indexOf(note)
                    if(selectedNotes.includes(noteIndex)) {
                        deSelect(noteIndex)
                    } else {
                        select(noteIndex)
                    }
                }
                if(playNoteOnTouch) {
                    playNote(notes.indexOf(note))
                }

                break;

            }

        }



        if(!hoveredNote && !movingNote) {
            createNote()
            deSelect('all')
        }

    }
    }
});



function createNote() {
                // Snap to Grid

                snapTime = Math.floor(x / cellWidth)/barDivisor;
            

                heightFromTopNote = y-topLimit
    
                numNotesDown = Math.floor(heightFromTopNote/cellHeight)
                
    
                snapNote = numNotesDown + (topLimit/cellHeight);
    
                numPixelsDown = (snapNote+1)*cellHeight
    
                num12down = numPixelsDown/defaultNoteHeight
    
                
    
                key = 127-num12down
    
               
               detune = 0
    
               
    
                // Create Note Object
                note = { 
                    x: snapTime, 
                    y: snapNote,
                    width: lastWidth,
                    temp: temperament,
                    end: snapTime + 1/barDivisor,
                    height: cellHeight/canvas.height, 
                    color: "blue" ,
                    isPlaying: false,
                    num : key,
                    divisor: barDivisor
                };
    
                console.log(note.num)
                notes.push(note);

                sendMidi()

                if(!playing) {
                    playNote(notes.indexOf(note))
                }
    
                // Redraw Notes
                updateNotes(note);

                let movingNote = note;
                isMoving = true;
}
// Draw Notes
function updateNotes(note, all=false, replacement = "false") {
    console.log(replacement)
    if(all){
        notesCtx.clearRect(0, 0, notesCanvas.width, notesCanvas.height);
        for (note of notes) {
            if(selectedNotes.includes(notes.indexOf(note))){
                notesCtx.fillStyle = "#444"
            } else {
                notesCtx.fillStyle = note.color;
            }
            notesCtx.fillRect(note.x*cellWidth*barDivisor + 1, note.y*note.height*canvas.height+1, note.width*barLength-2, note.height*canvas.height-2)
        }
    } else {
        notesCtx.clearRect(note.x*cellWidth*barDivisor, note.y*note.height*canvas.height, note.width*barLength, note.height*canvas.height)
        

        
        if(replacement != "false") {
            if(selectedNotes.includes(notes.indexOf(note))){
                notesCtx.fillStyle = "#444"
            } else {
                notesCtx.fillStyle = note.color;
            }
            
            
            index = notes.indexOf(note);
            
            if (index !== -1) {
                notes[index] = replacement; // Replace it in the global array
            }
            notesCtx.fillRect(replacement.x*cellWidth*barDivisor + 1, replacement.y*replacement.height*canvas.height+1, replacement.width*barLength-2, replacement.height*canvas.height-2);
        } else {
            if(selectedNotes.includes(notes.indexOf(note))){
                notesCtx.fillStyle = "#444"
            } else {
                notesCtx.fillStyle = note.color;
            }

            notesCtx.fillRect(note.x*cellWidth*barDivisor + 1, note.y*note.height*canvas.height+1, note.width*barLength-2, note.height*canvas.height-2);
        }

    }

    sendMidi()

}


   // Handle right-click (contextmenu) to delete note
   topLayer.addEventListener("contextmenu", function(e) {
    e.preventDefault(); // Prevent default context menu

    isDeleting = true
    // Check if any note is clicked
    findAndDeleteNote();
});

// Function to check for note at a specific position and delete it
function findAndDeleteNote() {


    // Loop through notes array to find the one at the clicked position
    for (let i = 0; i < notes.length; i++) {
        note = notes[i];
        if (x >= note.x*cellWidth*barDivisor && x <= (note.x*cellWidth*barDivisor + note.width*barLength) && y >= note.y*note.height*canvas.height && y <= (note.y*note.height*canvas.height + note.height*canvas.height)) {
            // Found the note, remove it from the array
            deleteNote(note, i)

            break;
        }
    }
}
function deleteNote(note, i){
    console.log(notes);
    notes.splice(i, 1);

    deSelect('all')

    console.log(notes); 

    // Clear the deleted note's area on the canvas
    notesCtx.clearRect(note.x*cellWidth*barDivisor, note.y*note.height*canvas.height, note.width*barLength, note.height*canvas.height);
    sendMidi()
}

let isDragging = false;
let draggingNote = null;
let edge = ""; // "left" or "right" to track which edge is being dragged
dragX = 0
dragY = 0

function getCanvasX(e) {
    rect = topLayer.getBoundingClientRect();
    x = e.clientX - rect.left;
    x = (x / rect.width) * topLayer.width;
    //x /= xscale
    return x
}

function getCanvasY(e) {
    rect = topLayer.getBoundingClientRect();
    y = e.clientY - rect.top;
    y = (y / rect.height) * topLayer.height
    //y /= yscale
    return y;
}

x = 0
y = 0

function getMouse(e) {

    

    getCanvasX(e)
                      
    getCanvasY(e)


}

topLayer.addEventListener("mousemove", function(e) {



    
    
    currentX = getCanvasX(e)
    dX = currentX - lastX

    currentY = getCanvasY(e)
    dY = currentY - lastY

    getMouse(e)

    let hoveredNote = null;
    let hoverPlayhead = false;

    let playHeadDiff = Math.abs(x-measure*barLength)

    if(draggingPlayhead) {
            cursorstyle = "ew-resize";
            measure += dX/barLength
            renderPlayhead()
            sendTime()
    }

 

    if(playHeadDiff < 5 || draggingPlayhead) {
        cursorstyle = "ew-resize";  // Right edge hover
    } else {
        if(isDeleting) {
        cursorstyle = "crosshair";  // Close to pencil
        }  else  {
        cursorstyle = "default"
        }

    



    // Loop through notes to detect if hovering over a left or right edge
    for (let i = 0; i < notes.length; i++) {
        note = notes[i];
        // Check if mouse is near the left or right edge of the note
        if (x >= note.x*cellWidth*barDivisor - 5 && x <= note.x*cellWidth*barDivisor + 5 && y >= note.y*note.height*canvas.height && y <= note.y*note.height*canvas.height + note.height*canvas.height) {
            hoveredNote = note;
            cursorstyle = "ew-resize";  // Left edge hover
        } else if (x >= note.x*cellWidth*barDivisor + note.width*barLength - 5 && x <= note.x*cellWidth*barDivisor + note.width*barLength + 5 && y >= note.y*note.height*canvas.height && y <= note.y*note.height*canvas.height + note.height*canvas.height) {
            hoveredNote = note;
            cursorstyle = "ew-resize";  // Right edge hover
        }


        if ( x >= note.x*cellWidth*barDivisor && x <= (note.x*cellWidth*barDivisor + note.width*barLength) && y >= note.y*note.height*canvas.height && y <= (note.y*note.height*canvas.height + note.height*canvas.height)) {
            if(isDeleting) {
                note = notes[i];
                console.log('deleting note '+i)
                deleteNote(note,i)
                i = 0
            } else {
                movingNote = note
                cursorstyle = "grab"
            }
        } 
    }


    if (!hoveredNote && !isDragging && !isMoving && !movingNote) {
        // No note hovered, reset cursor to default
        cursorstyle= "default";
    }


    if (isDragging && draggingNote) {
        if (Math.abs(dragX) >= barLength / barDivisor) {
            let newNote = JSON.parse(JSON.stringify(draggingNote));
            let newX, newWidth;
    
            if (edge === "left") {
                newX = draggingNote.x + (Math.round(dragX * barDivisor/barLength) / barDivisor);
                newWidth = draggingNote.width - (Math.round(dragX * barDivisor/barLength) / barDivisor);
    
            } else if (edge === "right") {
                newWidth = draggingNote.width + (Math.round(dragX * barDivisor/barLength) / barDivisor);
                newX = draggingNote.x
      
            }

            if (newWidth > 0) { // Prevent negative width
                newNote.x = newX;
                newNote.width = newWidth;
                dragX -= (Math.round(dragX * barDivisor) / barDivisor);
            }

            lastWidth = newWidth
            updateNotes(draggingNote, false, newNote);
            draggingNote = newNote;
        } else {
            dragX += dX;
        }
    }

    if (isMoving && movingNote) {

        let newNote = JSON.parse(JSON.stringify(movingNote));
        let newY, newX;
        if (Math.abs(dragX) >= barLength / barDivisor) {

            if(!isCtrlPressed) {
                newX = movingNote.x + (Math.round(dragX * barDivisor/barLength) / barDivisor);
                newY = movingNote.y

                

                newNote.x = newX;
                newNote.y = newY;

                updateNotes(movingNote, false, newNote);
                movingNote = newNote;
            } else {
                for(index of selectedNotes) {
                    movingIndex = notes[index]
                    console.log(index)
                    newX = movingIndex.x + (Math.round(dragX * barDivisor/barLength) / barDivisor);
                    newY = movingIndex.y
    
                    
    
                    newNote.x = newX;
                    newNote.y = newY;
    
                    updateNotes(notes[index], false, newNote);
                    notes[index] = newNote;
                }
            }
            dragX -= (Math.round(dragX * barDivisor) / barDivisor);

        } else {
            dragX += dX;
        }
            
        if (Math.abs(dragY) >= cellHeight) {
            console.log('dragging y')
            newX = movingNote.x
            newY = movingNote.y + (Math.round(dragY / cellHeight) );

            dragY -= (Math.round(dragY * barDivisor) / barDivisor);

            newNote.x = newX;
            newNote.y = newY;

            updateNotes(movingNote, false, newNote);
            movingNote = newNote;
        } else {

            dragY += dY;
        }
    

    }
    
}
lastX = currentX;
lastY = currentY;
topLayer.style.cursor = cursorstyle
});


document.addEventListener("mouseup", function(e) {
    // End dragging
    isDragging = false;
    draggingNote = null;
    edge = "";
    draggingPlayhead = false

    isDeleting = false
    isMoving = false
    movingNote = null;
});






function zoom(axis, e, direction) {

    getMouse(e)

    let mouseX = x/notesCanvas.width * 100;
    let mouseY = y/notesCanvas.height * 100;

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
    
    transform = `scale(${xscale}, ${yscale})`
    transformOrigin = `${mouseX}% ${mouseY}%`;

    
rect = pianoroll.getBoundingClientRect();

if(axis == 2){
    keys12.style.transformOrigin = `center ${mouseY}%`;
    keys12.style.transform = transform
}

    
let children = [
    canvas,
    notesCanvas,
    playheadCanvas,
    topLayer
]


for(let el of children) {
    el.style.transformOrigin = transformOrigin;
    el.style.transform = transform;
}
}

playing = false;

prevMeasure = 0

function renderPlayhead() {


 playhead.clearRect(prevMeasure*barLength-2, 0, 6, playheadCanvas.height);
 playhead.strokeStyle = "red"; // Grid line color
 playhead.lineWidth = 2;
 playhead.beginPath();
 playhead.moveTo(measure*barLength, 0);
 playhead.lineTo(measure*barLength, playheadCanvas.height);
 playhead.stroke();


prevMeasure = measure

}






function sendMidi() {
    msg = {
        type: 'midiUpdate',
        content: notes
    }
    channel.postMessage(msg);
}
// In the piano roll window (Sender)
const channel = new BroadcastChannel('sync_channel');

channel.onmessage = (event) => {
    if(event.data.type == 'midiUpdateRequest'){
        sendMidi();
    }
    if(event.data.type == 'timeUpdate' && draggingPlayhead == false) {
        measure = event.data.measure
        playing = event.data.playing
        renderPlayhead();
    }
    if(event.data.type == 'midiUpdate') {
        for(i in event.data.content.length) {
            notes[i].isPlaying = true
        }
    }

};


function sendTime() {
    msg = {
        type: 'timeUpdate',
        measure: measure,
        playing: playing
    }
    channel.postMessage(msg)
}

function playNote(noteIndex) {
    msg = {
        type: 'playNote',
        noteIndex: noteIndex
    }
    channel.postMessage(msg)
    
}
