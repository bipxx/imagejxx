"""Optional Dear ImGui desktop and toolkit-independent document models.

Importing this package opens no windows and does not load the native UI module.
"""


def __getattr__(name):
    if name == "Editor":
        from .app import Editor

        return Editor
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def main(argv=None):
    from .app import main as run

    return run(argv)


def launch(images=(), *, preferences=None, hidden=False, frames=0, screenshot=""):
    """Open the shared C++ ImGui UI from Python, returning the editor on exit.

    ``images`` accepts NumPy arrays, file paths, or Python ``Document`` objects.
    Live image processing stays in C++; ``editor.documents`` exposes snapshots.
    File-only input is staged as one native startup batch. Mixed input captures
    images and appends in order, pausing for each TIFF's review. Cancel keeps the
    accepted prefix and discards the remaining inputs. Workspace restore is
    suppressed for mixed input.
    """
    from pathlib import Path
    from .app import Editor

    images = list(images)
    if all(isinstance(image, (str, Path)) for image in images):
        editor = Editor(preferences=preferences, images=images or None)
    else:
        editor = Editor(preferences=preferences, restore_workspace=False)
        editor.append_launch_items(images)
    editor.run(hidden=hidden, frames=frames, screenshot=screenshot)
    return editor
