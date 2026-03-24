import sys
import ollama

def distill_context(file_paths):
    context_data = ""
    for path in file_paths:
        with open(path, 'r') as f:
            context_data += f"\n--- FILE: {path} ---\n{f.read()}\n"

    prompt = f"""
    You are a code context distiller. Below is a set of source files. 
    Your goal is to summarize the core logic, API signatures, and dependency 
    relationships so a remote LLM can perform a refactor. 
    
    RULES:
    - Strip all boilerplate and repetitive comments.
    - Keep all public method signatures.
    - Explain how these files interact.
    - Max 500 words.
    
    CODE TO DISTILL:
    {context_data}
    """

    response = ollama.generate(model='qwen2.5-coder:7b', prompt=prompt)
    return response['response']

if __name__ == "__main__":
    # AG will pass file paths as arguments
    paths = sys.argv[1:]
    print(distill_context(paths))