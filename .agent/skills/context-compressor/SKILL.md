---
name: context-distiller
description: Use this to condense large codebases or multiple files into a high-signal summary before deep analysis.
---

# Goal
Minimize token usage and latency by using a local LLM to pre-process code context.

# Instructions
1. When a task requires analyzing multiple files (especially C++), identify the relevant file paths.
2. Execute the local distillation script: `python3 scripts/distill.py {{file_paths}}`
3. Use the script's output as the "Executive Summary" to inform your final response.

# Constraints
- Do not send the raw code to the remote LLM if the local distillation is successful.
- If the local script fails (e.g., Ollama is not running), fall back to standard file reading.