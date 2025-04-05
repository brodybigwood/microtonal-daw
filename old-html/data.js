notes = []

const channel = new BroadcastChannel('sync_channel');

channel.onmessage = (event) => {
    console.log('Received data:', event.data);
    if(event.data.type == 'midiUpdate') {
        notes = event.data.content
        console.log(notes)
    }
    if(event.data.type == 'timeUpdate') {
        measure = event.data.measure
        isAnimating = event.data.playing
        console.log(measure)
    }
    if(event.data.type == 'togglePlay') {
        togglePlay()

    }
    if(event.data.type == 'return') {
        togglePlay(stop)
        measure = 0
        sendTime()
    }
    if(event.data.type == 'playNote') {
        index = event.data.noteIndex
        midiOn(notes[index])
    }
};

function sendTime() {
    msg = {
        type: 'timeUpdate',
        measure: measure,
        playing: isAnimating
    }
    channel.postMessage(msg)
}

function getMidi() {
    msg = {
        type: 'midiUpdateRequest'
    }
    channel.postMessage(msg)
}

let isAnimating = false;
let animationFrameId;


fps = 60

const frameDuration = 1000 / fps;  // Target frame rate (60 FPS)

measure = 0


Tone.Transport.bpm.value = 140; // Set the tempo to 120 BPM

barLengthSeconds = 4*60/Tone.Transport.bpm.value


function animate(time) {



    let frameTime = Date.now() - frameStartTime;  // Calculate elapsed time

    let renderTime = Date.now() - lastRenderTime

    if (frameTime >= frameDuration) {  // If a second has passed
        
          // Convert to seconds
          sendTime();

       
          
        frameStartTime = Date.now();  // Reset the start time for the next second
    }

    measure += (renderTime/1000) /barLengthSeconds
    for(let i = 0; i<notes.length; i++) {
        if((!notes[i].isPlaying) && (notes[i].x <= measure) && (notes[i].end >= measure)) {
            midiOn(notes[i])
        } 
        
    }


   lastRenderTime = Date.now()
    
    if (isAnimating) {

        animationFrameId = requestAnimationFrame(animate);


    }
}

function resetNotes() {
    for(let note of notes) {
        note.isPlaying = false
        sendMidi()
    }
    for(let voice of voices) {
        voice.synth.triggerRelease()
        voice.isPlaying = false;
    }
}
lastMeasure = 0
// Start the animation
function togglePlay(stop=false) {

    
    

    if (!isAnimating && stop == false) {
        isAnimating = true;
        startTime = Date.now();  // Initialize once outside the loop
        animationFrameId = requestAnimationFrame(animate);
        lastRenderTime = Date.now()
        frameStartTime = Date.now()
        lastMeasure = measure
    } else
    if (isAnimating) {
        isAnimating = false;
        cancelAnimationFrame(animationFrameId);
        measure = lastMeasure
        sendTime();
        resetNotes()
    }

}


function midiOn(note) {
    note.isPlaying = true
    midiNum = note.num
    semitonesOff = midiNum-69
    freqRatio = Math.pow(2,(semitonesOff)/12)
    frequency = 440* freqRatio

    let osc;

    //look for free osc
    for(let i = 0; i<voices.length; i++){
        if(!voices[i].isPlaying) {
            voices[i].isPlaying = true

            voices[i].playNote(frequency,barLengthSeconds*note.width, Tone.now())
            setTimeout(() => {

                voices[i].isPlaying = false
                note.isPlaying = false
            }, barLengthSeconds*note.width*1000); 
            break
        }

    }
sendMidi()
}



// create web audio api context


// create Oscillator nodes



voices = [];

function createWebSynth() {

}

function createToneSynth() {

    comp = new Tone.Compressor(
        {
            threshold: -24,   // Lower threshold to engage compression earlier (less distortion)
            knee: 30,         // Increase knee to make the onset of compression smoother and less aggressive
            ratio: 2,         // Reduce ratio to 2 for gentler compression (avoiding over-squashing)
            attack: 0.03,     // Slightly slower attack to allow transient peaks without too much squashing
            release: 0.8      // Longer release time to make sure the compressor lets go smoothly
        }
        
    )  

    gainNode = new Tone.Gain(0.3);  // Set the initial gain (volume)

    delay = new Tone.PingPongDelay({
        wet: 0.1, delayTime: "4n.", feedback: 0.1
    });

    reverb = new Tone.Reverb({
        wet: 0.3, decay: 3
    });

    filter = new Tone.Filter({
        frequency: 500,  // Initial frequency (you'll modulate this)
        type: "lowpass",  // Filter type (lowpass, highpass, bandpass, etc.)
        rolloff: -12      // Filter rolloff (filter steepness)
    })



    comp.connect(gainNode)    // Connect synth to delay
    gainNode.connect(filter)
    filter.connect(delay)
    delay.connect(reverb);  // Connect delay to convolver
    reverb.connect(Tone.Destination); // Connect convolver to gain node


for (let i = 0; i < 16; i++) {
    // Create a synth

 


    let synth = new Tone.Synth({
        envelope: {
            attack: 0.1,   // Time to reach full volume
            decay: 0.3,    // Time to decrease to sustain level
            sustain: 0.7,  // Sustain level (volume will hold here)
            release: 0.5   // Time it takes to fade out when released
        }
    }
    ); 
    synth.oscillator.type = "triangle";





    synth.connect(comp);  


    let voice = {
        synth: synth,
        filter: filter,
        delay: delay,
        reverb: reverb,
        gainNode: gainNode,
        playNote: function(note, duration) {
            // Trigger attack and release for all oscillators

            synth.triggerAttackRelease(note, duration); // Start sound

        },              // Store the Tone.js synth
        volume: gainNode.gain,      // Gain for controlling volume
        frequency: synth.frequency, // Frequency control
        isPlaying: false            // Whether the note is playing
    };
    
    // Add the voice to the array of synths
    voices.push(voice);
}




}

createToneSynth()


getMidi()


function sendMidi() {
    msg = {
        type: 'midiUpdate',
        content: notes
    }
    channel.postMessage(msg);
}

resetNotes()


// Check if Tone.js context has started
let isToneStarted = false;

// Function to start Tone.js when user clicks
document.addEventListener('click', () => {
    if (!isToneStarted) {
        Tone.start().then(() => {
            console.log("Tone.js AudioContext started!");
            isToneStarted = true; // Mark as started
      
        }).catch(err => {
            console.error("Failed to start Tone.js:", err);
        });
    }
});



if (Tone.context.state !== "running") {
    Tone.start().then(() => console.log("AudioContext resumed"));
}
