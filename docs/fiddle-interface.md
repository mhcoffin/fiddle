# Background

The Lua plugins are called 'Fiddles'. They are loaded into the mixer strips and transform Notes into other Notes.

# The Fiddle Interface

We want to be able to create Fiddles using natural language prompts to an LLM. So we need a clean interface for creating and editing Fiddles. The interface should be tailored to the task of creating and editing Fiddles, and should be easy to use. It should be accessible from the main window, and should be easy to find. It should be accessible from the main window, and should be easy to find.  

# Examples

'Create a Fiddle that shortens long notes by 10%'

'Create a Fiddle that finds ascending runs of notes and increases the dynamics. The increase should total 10% of the original dynamics, distributed across the run. '

'Create a Fiddle that finds descending pairs of notes and reduces the dynamics of the second note in the pair by 20%'.

'Create a Fiddle that detects whether the current notes are an inner line, and if so, reduces their dynamics by 10%'.

'Create a Fiddle that finds the top voice in a score and increases its dynamics by 10%'.




