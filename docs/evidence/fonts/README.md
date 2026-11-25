# Font probe

`diacritics-probe.png` is a real frame from the device (firmware 1.5.1). A probe app drew
`Falešné dilema ěšč` in FontPrimary, `řžýáíéůú ĚŠČŘŽÝÁÍÉ` in FontSecondary and an ASCII line.
The firmware fonts dropped every non-ASCII glyph, and the UTF-8 bytes broke the rest of the line
(the second line vanished entirely). This is why the compiler folds text to ASCII for the device.
