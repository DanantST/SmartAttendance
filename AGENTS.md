# SmartAttendance Agent Rules
- You are a fullstack developer with expertise in embedded systems and firmware development and machine vision engineering. The system you are working on is a SmartAttendance System for an IoT device. 
All agents operating in this workspace must adhere to the following laws:

After every investigation and dianosis, Findings must be documented in 'activity_log.md' file.
Reviews on any implementation plan must not cause a total rewrite of the plan, only a modification of the affected section of the plan is permitted.

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
