isCtrlPressed = false;
isZPressed = false;
isShiftPressed = false;
isScrollPressed = false;

// Listen for the 'keydown' event to detect when keys are pressed
document.addEventListener('keydown', function(event) {
  //event.preventDefault()
  if (event.code === 'Space'){
    event.preventDefault()
    togglePlay()
  }

  if (event.code === 'Enter'){
    event.preventDefault()
    beginning()
  }

  if (event.key === 'Control') {
    isCtrlPressed = true;
  }
  if (event.key === 'z') {
    isZPressed = true;
  }

  if (event.key === 'Shift') {
    isShiftPressed = true;
  }
  if (event.which === 2) {
    event.preventDefault()
    isScrollPressed = true;
  }

  if (event.key === 'a') {
    event.preventDefault()
    if(isCtrlPressed) {
      select('all')
    }
  }

  if(event.key === 'Delete') {
    for(index in selectedNotes) {
      deleteNote(notes[index])
    }
  }
});

// Listen for the 'keyup' event to detect when keys are released
document.addEventListener('keyup', function(event) {
  event.preventDefault()
  if (event.key === 'Control') {
    isCtrlPressed = false;
  }
  if (event.key === 'z') {
    isZPressed = false;
  }
  if (event.key === 'Shift') {
    isShiftPressed = false;
  }
  if (event.which === 2) {
    event.preventDefault()
    isScrollPressed = false;
  }
});

function togglePlay() {
  msg = {
    type: 'togglePlay'
}
channel.postMessage(msg);
}


function beginning() {
  msg = {
    type: 'return'
}
channel.postMessage(msg);
}

keys12 = document.getElementById('keys12')

document.addEventListener("wheel", function(e) {
 // e.preventDefault()

  if(e.deltaY < 0) {
      direction = 1;
  } else {
      direction = -1;
  }
  if (isCtrlPressed && isZPressed) {
    e.preventDefault()
      zoom(3, e, direction)
      return
  }
  if (isCtrlPressed) {
    e.preventDefault()

      zoom(1, e, direction)
  }
  if (isZPressed) {
    e.preventDefault()
      zoom(2, e, direction)
  }
  if (!isZPressed && !isCtrlPressed){
    requestAnimationFrame(() => {
      keys12.scrollTop += e.deltaY; 
  });
  }
},
{ passive: false });
;