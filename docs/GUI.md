# Using the GUI

The GUI is a desktop frontend for building, running, and comparing simulations.
Launch it from the repository root with:

```bash
make gui
```

The window is organized into tabs, with a shared control bar at the bottom that
runs whatever is in the **Runs** list.

## Typical workflow

1. **Setups** - create or edit a setup (the hardware description and its program
   code), then build it.
2. **Runs** - add one or more runs, each pairing a setup with a time limit, bit
   error rate, seed, and optional parameter overrides.
3. Press **Run** to execute the enabled runs across the selected number of cores.
4. **Results** - compare the metrics the runs produced.

The **Sweep** tab is a shortcut for step 2: it expands one or more parameter
ranges into many runs at once.

## The control bar

The bar below the tabs applies to every run in the Runs list:

- **Cores** - how many runs execute in parallel.
- **Timeout** - per-run wall-clock limit; a run exceeding it is stopped and
  marked failed.
- **Run** - start the enabled runs. **Cancel** stops an in-progress batch.
- The progress bar and the status line at the very bottom report progress.

## Setups tab

Lists the setups in the workspace. The buttons operate on the selected setup:

- **New...** - name a setup and open the editor to build it from scratch.
- **Edit...** - open the selected setup in the editor (double-clicking a setup
  does the same).
- **Duplicate** - copy the setup under a new name.
- **Remove** - delete the setup from the workspace.
- **Build** - compile the setup's program code into a loadable library. A setup
  must be built before it can run; rebuild it after editing. Build failures open
  a log window.

### The setup editor

The editor is a node graph of the system:

- Each **node** is a chiplet. Select one to edit its name, type, config, and its
  accelerators and interconnects in the side panel.
- Use the toolbar to add or remove chiplets.
- **Connections** are drawn by dragging from one interconnect port to another.
  Select a connection to edit its properties. Invalid connections are reported
  in a popup.
- **Open program code** writes the setup to disk and opens its `program.cpp` in
  your default editor. The link beside it opens the program-code guide.

## Runs tab

Each row is one simulation. Columns show the enabled state, label, setup, time
limit, BER, seed, a summary of overrides, and status.

- **Add...** / **Edit...** open the run editor, where you pick the setup and set
  the time, bit error rate, seed, and per-instance parameter overrides. A blank
  field uses the setup's default.
- **Duplicate**, **Remove**, and **Clear** manage the list.
- The **Enabled** checkbox controls whether a row runs; only enabled rows execute
  when you press Run.
- **Log level** sets the simulator verbosity for the whole batch.
- The **Output** panel shows the live log of the currently selected run. Select a
  different row to view its output; **Clear** empties the panel.

## Sweep tab

Generates many runs by varying parameters:

- **Base configuration** - the setup and the base time, BER, and seed shared by
  every generated run.
- **Sweep axes** - each axis varies one parameter. Enter values as a list
  (`128, 256, 512`) or a range (`start:stop:step`). Add more axes to sweep
  several parameters together; the run count is their product.
- **Add runs to list** appends the expanded runs to the Runs tab, where you can
  review and run them.

## Results tab

Populated after a batch finishes; only successful runs appear.

- The **table** has one row per run and one column per metric found in the
  statistics output. Type in the filter box to show only matching columns.
- The **chart** plots the selected metric across runs, one bar per run (numbered
  to match the table). Choose the metric from the drop-down.
- **Export CSV...** saves the full table to a file.
