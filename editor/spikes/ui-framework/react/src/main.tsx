import { useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "../../common.css";

function App() {
  const [name, setName] = useState("");
  const [saved, setSaved] = useState("");
  const dialog = useRef<HTMLDialogElement>(null);
  const opener = useRef<HTMLButtonElement>(null);

  function closeDialog() {
    dialog.current?.close();
    opener.current?.focus();
  }

  return (
    <>
      <a className="skip-link" href="#main">Skip to editor</a>
      <header>
        <h1>Grandleon Editor</h1>
        <p>Training Project · Unsaved changes</p>
        <nav aria-label="Project">
          <ul><li>Content</li><li>Maps</li><li>Diagnostics</li></ul>
        </nav>
      </header>
      <main id="main" tabIndex={-1}>
        <h2>Project settings</h2>
        {!name && <p id="errors"><a href="#project-name">Project name is required</a></p>}
        <form onSubmit={(event) => { event.preventDefault(); setSaved("Project saved"); }}>
          <label htmlFor="project-name">Project name</label>
          <input id="project-name" value={name} aria-invalid={!name}
            aria-describedby={!name ? "errors" : undefined}
            onChange={(event) => setName(event.currentTarget.value)} />
          <label htmlFor="target">Target profile</label>
          <select id="target"><option>Portable</option><option>Desktop</option></select>
          <button type="submit">Save</button>
          <button type="button" ref={opener} onClick={() => dialog.current?.showModal()}>
            Open command help
          </button>
        </form>
        <p aria-live="polite">{saved}</p>
      </main>
      <dialog ref={dialog} onCancel={closeDialog}>
        <h2>Command help</h2>
        <p>Use the project navigation to choose an editor.</p>
        <button autoFocus onClick={closeDialog}>Close</button>
      </dialog>
    </>
  );
}

createRoot(document.getElementById("app")!).render(<App />);
