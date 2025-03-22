let isCtrlPressed = false;
let isShiftPressed = false;

// Listen for the 'keydown' event to detect when keys are pressed
document.addEventListener('keydown', function(event) {
  if (event.key === 'Control') {
    isCtrlPressed = true;
  }
  if (event.key === 'Shift') {
    isShiftPressed = true;
  }

  // Check the current state of Ctrl and Alt
  if (isCtrlPressed) {
    console.log("Ctrl is currently pressed");
  }
  if (isShiftPressed) {
    console.log("shift is currently pressed");
  }
});

// Listen for the 'keyup' event to detect when keys are released
document.addEventListener('keyup', function(event) {
  if (event.key === 'Control') {
    isCtrlPressed = false;
  }
  if (event.key === 'Shift') {
    isShiftPressed = false;
  }
});