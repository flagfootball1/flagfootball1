// micro:bit "solved" sound for the pattern puzzle (MakeCode)
//
// How to use: go to https://makecode.microbit.org, start a New Project,
// click "JavaScript" at the top, replace everything with this file,
// then Download it to the micro:bit.
//
// Wiring (micro:bit breakout -> master Arduino):
//   P1  -> Arduino pin 10
//   GND -> Arduino GND   (needed, or the signal won't work)
// Sound:
//   micro:bit V2: plays on the built-in speaker (and on P0).
//   micro:bit V1: no speaker, so connect a speaker/buzzer to P0 and GND.
//
// The Arduino pulls P1 to GND when the puzzle is solved and lets go of it
// when RESET is pressed. The tune plays once each time it is solved.

pins.setPull(DigitalPin.P1, PinPullMode.PullUp)

let played = false
basic.showIcon(IconNames.Asleep)

basic.forever(function () {
    let solved = pins.digitalReadPin(DigitalPin.P1) == 0

    if (solved && !played) {
        played = true
        basic.showIcon(IconNames.Happy)
        music.play(music.builtInPlayableMelody(Melodies.PowerUp), music.PlaybackMode.UntilDone)
    } else if (!solved && played) {
        // RESET was pressed: get ready for the next game.
        played = false
        basic.showIcon(IconNames.Asleep)
    }

    basic.pause(20)
})
