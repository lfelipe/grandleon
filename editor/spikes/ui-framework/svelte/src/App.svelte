<script lang="ts">
  let name = $state("");
  let saved = $state("");
  let dialog: HTMLDialogElement;
  let opener: HTMLButtonElement;

  function closeDialog() {
    dialog.close();
    opener.focus();
  }
</script>

<a class="skip-link" href="#main">Skip to editor</a>
<header>
  <h1>Grandleon Editor</h1>
  <p>Training Project · Unsaved changes</p>
  <nav aria-label="Project">
    <ul><li>Content</li><li>Maps</li><li>Diagnostics</li></ul>
  </nav>
</header>
<main id="main" tabindex="-1">
  <h2>Project settings</h2>
  {#if !name}<p id="errors"><a href="#project-name">Project name is required</a></p>{/if}
  <form onsubmit={(event) => { event.preventDefault(); saved = "Project saved"; }}>
    <label for="project-name">Project name</label>
    <input id="project-name" bind:value={name} aria-invalid={!name}
      aria-describedby={!name ? "errors" : undefined}>
    <label for="target">Target profile</label>
    <select id="target"><option>Portable</option><option>Desktop</option></select>
    <button type="submit">Save</button>
    <button type="button" bind:this={opener} onclick={() => dialog.showModal()}>
      Open command help
    </button>
  </form>
  <p aria-live="polite">{saved}</p>
</main>
<dialog bind:this={dialog} oncancel={closeDialog}>
  <h2>Command help</h2>
  <p>Use the project navigation to choose an editor.</p>
  <button autofocus onclick={closeDialog}>Close</button>
</dialog>
