// Copy buttons. Each one copies the whole file, with the two lines every checked
// file starts with, so that what is pasted is checked when it is run.
document.querySelectorAll("button.copy").forEach(function (button) {
  button.addEventListener("click", function () {
    var text = button.dataset.src;
    var done = function (label) {
      button.textContent = label;
      setTimeout(function () { button.textContent = "Copy"; }, 1500);
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () { done("Copied"); }, function () { select(button); done("Select + copy"); });
    } else {
      select(button);
      done("Select + copy");
    }
  });
});

// Where the clipboard is refused, select the code so the reader can copy it by hand.
function select(button) {
  var code = button.closest("figure").querySelector("pre.code");
  var range = document.createRange();
  range.selectNodeContents(code);
  var sel = window.getSelection();
  sel.removeAllRanges();
  sel.addRange(range);
}
