# SmartAttendance Agent Rules

All agents operating in this workspace must adhere to the following laws:

## 1. Activity Log Maintenance
- **Rule**: The agent MUST always update the `activity_log.md` file at the root of the workspace after every task run.
- **Format**: Each update must append a new session section using the following structure:
  ```markdown
  ## Session - YYYY-MM-DD - [Short Feature/Task Name]

  ### User Request
  > [User's original prompt or intent]

  ### Implementation
  > [details on how it was done]
  #### Feature Overview
  [Brief overview of what was implemented/fixed]

  **[File Path relative to root]** changes:
  - [Detailed list of changes in this file]

  #### Commit
  [Commit hash or message]
  ```
- **Filing**: Ensure changes are appended at the end of the `activity_log.md` file, separated by a `---` horizontal rule.

## 2. GitHub push
- After every task run, the agent must push the changes to the GitHub repository.
- The commit message should be concise and descriptive, following the conventional commits specification
