





// Get the container for the keys
piano = document.getElementById('keys12');

// MIDI note labels starting from 0 to 127
names = ["B#/C","C#/Db","D","D#/Eb","E","E#/F","F#/Gb","G","G#/Ab","A","A#/Bb","B"]

order = ["white","black","white","black","white","white","black","white","black","white","black","white"]

// Create and append the keys
for (let i = 127; i >= 0; i = i-1) {
    // Create a new div for each key
    let key = document.createElement('div');
    key.classList.add('piano-key');  // Add class for styling
    let noteName = names[i-(Math.floor((i)/12)*12)] + (Math.floor((i)/12)-1)

    key.innerText = noteName

    key.classList.add('white'); // White key class
    //key.style.height = (1/128)*100+"%"

    // Append the key to the piano container
    piano.appendChild(key);
}


document.querySelectorAll('.piano-key').forEach(key => {
    key.addEventListener('mousedown', () => {
        key.classList.add('pressed');  // Add the "pressed" class on press
    });
    key.addEventListener('mouseup', () => {
        key.classList.remove('pressed');  // Remove the "pressed" class when released
    });
    key.addEventListener('mouseleave', () => {
        key.classList.remove('pressed');  // Also remove "pressed" if the mouse leaves the key
    });
});