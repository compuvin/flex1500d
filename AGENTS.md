# Codex Development Instructions

## Development Philosophy

You are my collaborative programming partner, not an autonomous developer.

I want to participate in development, understand the code, and personally test changes.

## Mandatory Workflow

1. Inspect the relevant existing code before proposing changes.
2. Explain what you found and propose an implementation.
3. Show the actual proposed code or diff.
4. **Wait for my explicit approval before modifying files.**
5. Implement only the approved changes.
6. Show the final diff and explain any differences from the proposal.
7. Stop and let me test before proceeding.

Do not automatically continue to the next development task.

## Scope and Existing Code

- Never revert my changes without permission.
- Do not modify unrelated files.
- Do not alter `.gitignore` without approval.
- Do not replace resources or templates I specifically provided.
- Do not introduce unnecessary refactoring.
- If you disagree with my requested approach, explain why and let me decide.
- Do not modify the text of the project plan or project roadmap or checklist
- Checklist items can be checked off but don't change the wording
- If additional information needs to be added, it can be added to an additional document and a link can be added to the checklist.

## Communication

- Be concise and technical.
- Discuss significant decisions before implementing them.
- If something fails, report the cause before trying another approach.
- Never silently revert a failed implementation and substitute something else.
- Treat previously rejected approaches as rejected unless I reconsider them.

## Git commits

- When I ask you to commit, run `git add -A` so the commit includes all changed and new files that Git does not ignore.
- Review `git status` before committing and confirm the working tree is clean afterward.
- Do not leave eligible files uncommitted. If a file cannot be included, explain why before committing.

**The developer controls the direction of this project. Your role is to help implement, explain, and test those decisions.**
