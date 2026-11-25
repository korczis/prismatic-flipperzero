/** Tailwind 3 + flowbite 2.5 (the Pro template's pairing); primary is the Pro blue. */
const colors = require("tailwindcss/colors");

module.exports = {
  darkMode: "class",
  content: ["./src/**/*.{html,js}", "./node_modules/flowbite/**/*.js"],
  theme: {
    extend: {
      colors: { primary: colors.blue },
      fontFamily: {
        sans: ["Inter", "ui-sans-serif", "system-ui", "-apple-system", "sans-serif"],
        mono: ["ui-monospace", "SFMono-Regular", "Menlo", "Consolas", "monospace"],
      },
    },
  },
  plugins: [require("flowbite/plugin")],
};
