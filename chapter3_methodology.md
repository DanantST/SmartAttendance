# CHAPTER THREE

# MATERIALS AND METHODS

---

## 3.1 Introduction

This chapter presents the materials, design decisions, and implementation procedures that collectively constitute the methodology of the SmartAttendance system. The chapter is structured to satisfy the dual demands of engineering rigour and academic documentation. It begins by situating the work within an appropriate research design framework and justifying why that framework was chosen. It then catalogues the hardware components and software tools employed, before presenting the system architecture, the rationale behind each design choice, and the step-by-step implementation sequence followed during development. The chapter closes with a description of the data collection strategy, the statistical metrics used to evaluate system performance, the testing procedures applied, and the ethical considerations arising from a biometric system that captures and processes human facial data.

---

## 3.2 Research Design

### 3.2.1 Classification of the Study

This work is classified as **applied engineering research** conducted through an **experimental design-and-build methodology**. The primary objective was not to generate a new theoretical contribution to computer science or electrical engineering as disciplines, but rather to produce a functional artefact — a working smart attendance device — that integrates existing theoretical principles from embedded systems, computer vision, and networked computing into a coherent and deployable system. This distinction is important, as it determines the appropriate validation approach: the system is evaluated against performance benchmarks rather than against a hypothesis about a population.

The research is, in the terminology of Creswell and Creswell (2018), a **quantitative experimental study** in the sense that it produces measurable outcomes — recognition accuracy, processing latency, false acceptance rate — that are collected under controlled experimental conditions and analysed with statistical tools. However, unlike a pure experimental study, the independent variable is not a treatment applied to human participants but the design configuration of the system itself, which is iteratively modified and re-evaluated until acceptable performance is achieved.

### 3.2.2 Engineering Design Methodology

The design methodology adopted in this project is the **iterative prototyping model**, a well-established approach in embedded systems engineering. Under this model, the system is not designed in full before any implementation begins; instead, a sequence of increasingly complete prototypes is constructed, with each iteration exposing constraints and failure modes that inform the next design cycle. This is in contrast to a waterfall model, which demands a complete and stable specification before hardware is ordered or firmware is written — an approach that is impractical when working with a novel hardware platform where many parameters (bus timing, DMA latency, memory pressure under concurrent workloads) cannot be reliably predicted from datasheets alone.

The iterative prototyping model was organised into six sequential phases in this project:

1. **Phase 1 — Baseline Establishment**: Setting up the toolchain, resolving the build system, and eliminating compiler warnings to achieve a clean, reproducible build environment.
2. **Phase 2 — Storage Integration**: Replacing stub database code with a real SQLite3 engine backed by a physical SD card.
3. **Phase 3 — Display and Touch Integration**: Bringing the 7-inch MIPI-DSI panel and GT911 touch controller into operation and rendering the graphical interface.
4. **Phase 4 — Network Connectivity**: Establishing Wi-Fi connectivity through the ESP32-C6 co-processor and activating cloud synchronisation.
5. **Phase 5 — AI Pipeline Activation**: Integrating face detection, geometric alignment, feature extraction, and similarity matching into a real-time recognition loop.
6. **Phase 6 — System Hardening**: Addressing security vulnerabilities, power management edge cases, concurrent access bugs, and watchdog configuration for production stability.

### 3.2.3 Justification of the Chosen Approach

The iterative prototyping model was chosen over alternatives for three reasons.

First, the hardware platform — the Elecrow CrowPanel Advanced 7-inch ESP32-P4 — was, at the time of project commencement, a recently released board with limited third-party documentation. Many device-specific parameters (such as the GT911 I2C address latch sequence and the EK79007 bridge command set) could only be established through direct experimentation, not by reading a specification. A waterfall approach would have required making assumptions about these parameters before implementation, which would have led to significant rework.

Second, the system integrates subsystems from markedly different domains — computer vision, real-time operating systems, relational databases, and web APIs. The interactions between these domains, particularly around memory pressure and task scheduling, could not be fully anticipated at the design stage. Iterative development allowed each interaction to be characterised and resolved before the next subsystem was added.

Third, iterative development aligns with best practices in embedded systems engineering, where hardware constraints impose hard limits on software design and where testing under realistic resource conditions is the only reliable means of validation. As Berger (2017) notes in the context of IoT firmware development, theoretical design review is insufficient when resource contention across concurrent tasks represents the dominant failure mode.

---

## 3.3 Materials

### 3.3.1 Hardware Components

The hardware components employed in this project are listed in Table 3.1, together with their primary functional role. A detailed justification of each selection is provided in Section 3.5.

**Table 3.1: Hardware Components and Their Functions**

| Component | Specification | Role in System |
|---|---|---|
| Elecrow CrowPanel Advanced 7" | ESP32-P4 SoC, dual-core LX7 @ 400 MHz | Main processing unit, AI inference, system control |
| Integrated camera module | AS-AG638A32 M1-25 (SC2336 sensor, MIPI-CSI 24-pin) | Real-time image acquisition |
| 7-inch IPS LCD panel | 1024 × 600 px, MIPI-DSI, EK79007 bridge | User interface display |
| GT911 capacitive touch panel | Multi-touch, I2C, 1024 × 600 sensing area | Touch input |
| MicroSD card | Class 10, minimum 8 GB, FAT32 | AI model storage, SQLite database, CSV reports |
| ESP32-C6 co-processor (on-board) | IEEE 802.11 b/g/n, Bluetooth 5.0, SDIO interface | Wi-Fi and BLE connectivity for ESP32-P4 |
| 10,000 mAh USB power bank | 5 V, 2 A output | Portable field power supply |
| PVC pipe enclosure | Custom-cut, primed and spray-painted | Protective device housing |
| M3 screws and standoffs | 8 mm, stainless steel | Internal mounting hardware |

### 3.3.2 Software Tools and Frameworks

The software tools, libraries, and frameworks used across the firmware and cloud components of the system are described below.

**ESP-IDF v5.3 (Espressif IoT Development Framework):** The primary firmware development framework for the ESP32 family of microcontrollers. ESP-IDF provides the FreeRTOS real-time operating system kernel, hardware abstraction layer drivers, a CMake-based build system, and the component management infrastructure through which third-party libraries are declared and resolved (Espressif Systems, 2024).

**ESP-DL:** Espressif's on-device deep learning inference library, optimised for integer quantisation (int8) and hardware acceleration on the ESP32-P4's high-performance core. ESP-DL was used for both the face detection models and the MobileFaceNet feature extractor.

**LVGL (Light and Versatile Graphics Library) v9:** An open-source embedded graphics library that provides a widget set, animation engine, and display driver interface for resource-constrained systems. LVGL was used to build the entire graphical user interface, including the camera preview, navigation bar, enrollment screen, settings screens, and attendance report views (LVGL, 2024).

**FreeRTOS:** A real-time operating system kernel that provides task scheduling, inter-task communication primitives (queues, semaphores, event groups), and memory management. FreeRTOS is incorporated into ESP-IDF and was used to structure all concurrent firmware operations (Barry & Hyett, 2017).

**SQLite3 (ESP-IDF port):** A serverless, file-based relational database engine ported to ESP-IDF. SQLite was used to implement the local attendance and user database stored on the SD card.

**Python 3.11 with python-telegram-bot v21.3:** Used to implement the cloud Telegram bot. The `python-telegram-bot` library provides an asynchronous wrapper around the Telegram Bot API, supporting both long-polling (for local development) and webhook-based operation (for cloud deployment).

**FastAPI with Uvicorn:** A modern Python web framework used to implement the REST API endpoints consumed by the ESP32-P4 device for bi-directional data synchronisation.

**Visual Studio Code with ESP-IDF Extension:** The primary integrated development environment used for firmware editing, IntelliSense-based code navigation, and serial monitor access.

**Git (version control):** All source code was maintained in a Git repository hosted on GitHub, enabling incremental development with the ability to revert to known-good states when a firmware change introduced a regression.

---

## 3.4 System Design

### 3.4.1 Overall Architecture

The SmartAttendance system is structured across three tiers, as illustrated conceptually in Figure 3.1.

```
┌──────────────────────────────────────────────────────────────────┐
│                     TIER 1 — EMBEDDED DEVICE                     │
│  ┌───────────┐  ┌───────────┐  ┌──────────┐  ┌──────────────┐  │
│  │  Camera   │→ │ Face Det. │→ │  Align.  │→ │  MobileFace  │  │
│  │  (CSI)   │  │  (ESP-DL) │  │ (112×112)│  │   Net Embed. │  │
│  └───────────┘  └───────────┘  └──────────┘  └──────┬───────┘  │
│                                                       │ Embedding│
│  ┌───────────┐  ┌───────────┐  ┌──────────────────── ▼────────┐ │
│  │  LVGL UI  │← │  SQLite   │← │  Recognizer (cosine sim.)   │ │
│  │ (7" MIPI) │  │  /sdcard  │  │  Attendance Logger (queue)  │ │
│  └───────────┘  └─────┬─────┘  └─────────────────────────────┘ │
│                        │ HTTPS sync (6h)                         │
└────────────────────────┼────────────────────────────────────────┘
                         │
┌────────────────────────┼────────────────────────────────────────┐
│                  TIER 2 — CLOUD BACKEND                          │
│   ┌──────────────┐     │     ┌────────────────────────────────┐ │
│   │  Telegram Bot│     │     │  FastAPI REST Server           │ │
│   │  (Lecturer   │     └────→│  POST /api/sync_users          │ │
│   │   scheduling)│           │  GET  /api/get_schedules        │ │
│   └──────┬───────┘           └────────────────────────────────┘ │
│          │  SQLite (bot_data.db)                                  │
└──────────┼─────────────────────────────────────────────────────┘
           │
┌──────────▼──────────────────────────────────────────────────────┐
│                  TIER 3 — USER INTERFACES                        │
│      Device Touchscreen (admin)    Telegram App (lecturers)      │
└─────────────────────────────────────────────────────────────────┘
```
*Figure 3.1: Three-tier system architecture of SmartAttendance.*

### 3.4.2 System-Level Operation Flow

The operational flow of the system follows a well-defined sequence during normal attendance-taking mode. When a person approaches the device, the embedded camera captures a continuous stream of frames at approximately 30 frames per second. Each frame is passed to the face detection module, which scans the image for faces meeting the minimum confidence and size thresholds. When a valid face is detected, the system localises five facial landmarks (both eyes, nose tip, and both mouth corners), uses these to perform a geometric alignment transformation, and passes the normalised 112 × 112 pixel face image to the MobileFaceNet feature extractor. The resulting 128-dimensional embedding vector is compared against the in-memory cache of all enrolled user embeddings using cosine similarity. If the closest match exceeds the recognition threshold of 0.65, the user is identified, a bounding box with their name is overlaid on the live preview, and an attendance record is queued for asynchronous insertion into the SQLite database. If no match exceeds the threshold, the face is labelled as unknown and no record is written.

Periodically — on a six-hour timer, or immediately on administrator request — the device connects to Wi-Fi and synchronises with the cloud backend: pushing enrolled user data outward, pulling new lecture schedules inward, and optionally transmitting a CSV attendance report to the administrator's Telegram account.

### 3.4.3 State Machine Design

To prevent subsystem conflicts, the firmware was organised around a global finite state machine with seven states, as shown in Figure 3.2. Every task interrogates the current state before performing its primary operation.

```
                  ┌─────────────┐
    On power-on   │    NORMAL   │ ← Recognition active
       ─────────→ │  (default)  │
                  └──────┬──────┘
           ┌─────────────┼──────────────┐
           ▼             ▼              ▼
     ┌──────────┐  ┌──────────┐  ┌──────────┐
     │ENROLLMENT│  │ SETTINGS │  │ SYNCING  │
     └──────────┘  └──────────┘  └──────────┘
           ▼
     ┌──────────┐
     │ REPORTS  │
     └──────────┘
           (All transitions return to NORMAL on completion)

     LOW_BATTERY → graceful warning → SHUTDOWN
```
*Figure 3.2: SmartAttendance global state machine.*

---

## 3.5 Hardware Design

### 3.5.1 Main Controller: ESP32-P4

The selection of the ESP32-P4 as the main controller was driven by three primary requirements: sufficient raw compute for on-device AI inference, native high-speed camera and display interfaces, and an adequate memory architecture to hold frame buffers and model weights concurrently.

The ESP32-P4 features a dual-core Xtensa LX7 processor clocked at 400 MHz, alongside a high-performance core (HP-CORE) that provides hardware acceleration for the integer matrix multiplications central to neural network inference. Espressif's ESP-DL library exploits this unit to run quantised (int8) models at speeds that would be unachievable through pure software computation at this clock frequency. In testing, the MobileFaceNet embedding extraction step completes in approximately 80–100 milliseconds per face, which is adequate for a 30 fps recognition loop with several hundred milliseconds between successive recognition decisions.

The ESP32-P4 also integrates a MIPI-CSI (Camera Serial Interface) receiver and a MIPI-DSI (Display Serial Interface) transmitter, both of which are hardware standards designed for high pixel bandwidth. These hardware peripherals eliminate the need for the slower SPI-based camera and display buses used in previous ESP32 generations, enabling simultaneous 1280 × 720 camera capture and 1024 × 600 display rendering without saturating the CPU.

Critically, the ESP32-P4 supports a 16-line HEX-mode PSRAM at 200 MHz, providing approximately 32 MB of external RAM. This capacity is necessary to satisfy the combined memory demands of the LVGL double frame buffers (approximately 3.7 MB), the MobileFaceNet model weights (approximately 2 MB), the SQLite page cache (configured at 8 MB), and the in-memory embedding cache for up to 500 enrolled users.

The on-board ESP32-C6 co-processor, connected to the P4 over SDIO using Espressif's `esp_hosted` framework, provides Wi-Fi (IEEE 802.11 b/g/n) and Bluetooth 5.0 without requiring a separate radio module. From the firmware perspective, the hosted transport is transparent: standard `esp_wifi_*` and NimBLE APIs are used without modification.

### 3.5.2 Camera Module: SC2336 Sensor

The camera module (AS-AG638A32 M1-25) incorporates a SmartSens SC2336 1/2.8-inch CMOS image sensor with a 24-pin MIPI-CSI interface. This module was retained from the CrowPanel board's default configuration, but its parameters required careful characterisation before integration.

The SC2336 uses an SCCB (Serial Camera Control Bus) address of 0x36, its native Bayer colour filter arrangement is BGGR (Blue-Green-Green-Red), and its output is processed by the ESP32-P4's built-in Image Signal Processor (ISP) to yield RGB565 pixel data suitable for direct processing by ESP-DL models. The camera operates at a single supported configuration: 1280 × 720 pixels at 30 fps. For the recognition pipeline, frames are captured at 320 × 240 (QVGA resolution) to reduce detection latency, while the enrollment flow uses 640 × 480 (VGA) to maximise the face detail available for quality assessment.

Indoor exposure parameters were determined empirically. The configuration `exposure_us = 15,000`, `exposure_val = 1,100`, `gain = 4,000` was found to produce well-illuminated images under fluorescent overhead lighting with minimal motion blur from rolling shutter effects.

### 3.5.3 Display: 7-Inch MIPI-DSI IPS Panel

The 7-inch IPS panel (1024 × 600 px) offers a significantly larger user interaction surface than the smaller TFT modules typically used in embedded attendance systems, which is important for legibility across a classroom environment. The MIPI-DSI interface, through the EK79007 bridge IC, supports a 900 Mbps, 2-lane data link with a pixel clock of 51 MHz, which is sufficient to sustain full-resolution rendering at the display's native refresh rate.

Backlight intensity is controlled through a LEDC PWM channel on GPIO 31, operating at 30 kHz with an 11-bit duty resolution. This allows smooth brightness adjustment from a settings menu without visible flicker, which is relevant when the device is used in a dimly lit seminar room.

### 3.5.4 MicroSD Card for Persistent Storage

A MicroSD card provides the only persistent writable storage in the system. Internal SPI flash on the ESP32-P4 is insufficient in capacity for the three data categories it must hold: the AI model weight files (approximately 4 MB across three model files), the SQLite attendance database (growing with each enrollment and attendance record), and the CSV export files generated for cloud synchronisation.

The SD card is accessed over SDMMC 1-bit mode (a single-wire data interface using GPIO 43 for CLK, GPIO 44 for CMD, and GPIO 39 for D0), which was the configuration supported by the available CrowPanel GPIO allocation. The filesystem is FAT32, mounted at `/sdcard`. The partition table allocates a 6 MB factory application partition and a 16 MB total flash size to accommodate the large firmware binary produced by the LVGL and ESP-DL dependencies.

### 3.5.5 Power Supply

A 10,000 mAh USB power bank was selected as the field power source. The ESP32-P4 board accepts 5 V via a USB-C connector and draws approximately 1.5–2.5 W during active recognition, with peak current during Wi-Fi transmission reaching approximately 0.5 A. At an average consumption of 1.8 W, the 10,000 mAh (37 Wh) bank provides a theoretical operational duration of approximately 20 hours, which is sufficient for a full academic day without recharging.

### 3.5.6 Enclosure

The enclosure was fabricated from PVC pipe cut and shaped to house the CrowPanel board with the 7-inch screen face-forward and the camera module oriented toward the room entrance. The enclosure was primed, spray-painted, and fitted with M3 brass standoffs to secure the PCB without mechanical stress on the board connectors. This approach was selected over 3D-printed alternatives for reasons of cost and availability, as PVC pipe is readily obtainable in Nigeria and does not require specialised fabrication equipment.

---

## 3.6 Software Design

### 3.6.1 Firmware Architecture: FreeRTOS Task Model

The firmware is structured as a collection of concurrent FreeRTOS tasks, each responsible for a single well-defined system function. FreeRTOS was chosen because it is the RTOS kernel embedded in ESP-IDF, provides deterministic scheduling of tasks with configurable priorities and stack sizes, and offers a rich set of inter-task communication primitives that allow subsystems to exchange data without polling or shared global variables (Barry & Hyett, 2017).

Five persistent tasks run throughout the device's operational lifetime, as described in Table 3.2.

**Table 3.2: FreeRTOS Task Configuration**

| Task Name | Priority | Stack Size | Function |
|---|---|---|---|
| `camera_task` | 7 | 8,192 bytes | Camera frame capture and queuing |
| `detection_recognition_task` | 8 | 16,384 bytes | Face detection, alignment, embedding, matching, attendance logging |
| `db_task` | 6 | 8,192 bytes | Serialised SQLite operations |
| `network_sync_task` | 5 | 16,384 bytes | Wi-Fi connection, cloud synchronisation |
| `battery_task` | 4 | 4,096 bytes | Battery ADC monitoring, idle sleep management |

Task priorities were assigned so that detection and recognition (the most latency-sensitive operation) runs at the highest priority below the LVGL handler, camera capture runs slightly below it, and background operations such as database writes and network sync run at lower priorities so they cannot starve the foreground vision pipeline.

### 3.6.2 Inter-Task Communication

Tasks exchange data exclusively through FreeRTOS typed queues, not through shared memory. This architectural decision eliminates an entire class of concurrency bugs caused by unprotected global state. The communication channels are:

- **`g_camera_frame_queue`** (depth 1, element type `camera_frame_t`): Transfers camera frames from the camera task to the detection task. The queue depth of one ensures that only the most recently captured frame is ever processed — older frames are discarded rather than queued, preventing the recognition result from lagging behind the physical world.
- **`g_db_request_queue`** (depth 20, element type `db_request_t`): Transfers attendance log insertion requests from the detection task to the database task, serialising all SQLite write operations onto a single thread.
- **`g_system_event_queue`** (depth 50, element type `system_event_t`): Carries system-wide events (low battery, recognition success, sync complete) between tasks.
- **`g_system_event_group`**: An FreeRTOS event group used for lightweight bit-flag signalling, such as notifying the network task to begin an immediate synchronisation cycle.

State-access contention is handled by a dedicated state mutex (`g_state_mutex`) that guards reads and writes to the global `system_state_t` variable. Every task acquires the mutex before reading or writing the state, ensuring that state transitions are atomic from the perspective of all concurrent observers.

### 3.6.3 Face Detection Algorithm

Face detection is performed using a two-stage cascaded convolutional neural network provided by Espressif's ESP-DL library. The architecture is conceptually similar to the Multitask Cascaded Convolutional Network (MTCNN) described by Zhang et al. (2016), in which a rapid proposal stage scans the image at multiple scales to generate candidate face regions, and a refinement stage scores and filters those candidates using a more accurate but computationally costlier network. The two model files used (`human_face_detect_msr_s8_v1.espdl` for the proposal stage and `human_face_detect_mnp_s8_v1.espdl` for the refinement stage) are loaded from the SD card into PSRAM at boot time.

The detector accepts a raw RGB565 frame buffer and returns a list of detected face regions, each described by:
- A bounding box (x, y, width, height) in pixel coordinates
- A detection confidence score in the range [0, 1]
- Five facial landmark coordinates: left eye, right eye, nose tip, left mouth corner, right mouth corner

Two quality gates are applied to each detection before it is forwarded to the alignment stage:

1. **Minimum confidence**: Detections with a confidence score below 0.60 are discarded. This value was established empirically to eliminate distant or partially visible faces that would yield unreliable embeddings.
2. **Minimum face size**: Bounding boxes with a width or height below 50 pixels are discarded. Faces smaller than this threshold typically contain fewer than the number of identifiable pixels required for the MobileFaceNet model to produce a discriminative embedding.

In addition, three per-frame quality metrics are computed for use during the enrollment procedure (described in Section 3.8):

**Sharpness** is estimated as the spatial variance of the green channel of the face ROI, which approximates the Laplacian variance — a widely used proxy for image focus quality. A numerically higher variance indicates a sharper image.

**Brightness** is computed as the luminance-weighted average of each pixel in the face bounding box, according to the ITU-R BT.601 standard luma formula:

$$Y = 0.299R + 0.587G + 0.114B \tag{3.1}$$

where R, G, and B are the red, green, and blue channel values (0–255) of each pixel. Frames with a mean luma below 40 or above 200 are rejected as too dark or too overexposed.

**Head yaw angle** is estimated geometrically from the detected landmark positions. The horizontal deviation of the nose tip from the midpoint of the two eyes is normalised by the inter-eye distance and converted to degrees:

$$\hat{\theta}_{yaw} = \frac{x_{nose} - \frac{x_{left\_eye} + x_{right\_eye}}{2}}{x_{right\_eye} - x_{left\_eye}} \times 90^{\circ} \tag{3.2}$$

Frames where $|\hat{\theta}_{yaw}| > 25°$ are rejected to ensure that only near-frontal face samples contribute to the enrollment template.

### 3.6.4 Geometric Face Alignment

Before feature extraction, each detected face is geometrically normalised to a canonical 112 × 112 pixel view using a similarity transformation. A similarity transformation preserves the shape of the face (no shearing or skewing) but corrects for translation, rotation, and scale, mapping the five detected facial landmarks to a fixed reference set of target coordinates defined by the MobileFaceNet training data. This normalisation step is critical: Chen et al. (2018) specifically note that consistent geometric alignment is a prerequisite for the embedding model to produce stable, discriminative feature vectors.

The transformation matrix **M** is computed by minimising the least-squares error between the detected landmark positions and the target reference positions. Given detected landmarks $\mathbf{p}_i$ and reference positions $\mathbf{q}_i$ for $i = 1...5$:

$$\mathbf{M} = \arg\min_{\mathbf{M}} \sum_{i=1}^{5} \|\mathbf{M}\mathbf{p}_i - \mathbf{q}_i\|^2 \tag{3.3}$$

The resulting matrix is applied to the source frame buffer using bilinear interpolation to produce the aligned 112 × 112 × 3 RGB output. All intermediate buffers for this step are allocated from PSRAM using `heap_caps_malloc(MALLOC_CAP_SPIRAM)` to avoid overflowing the 16 KB task stack.

### 3.6.5 Feature Extraction: MobileFaceNet

Feature extraction is performed by a quantised (int8) implementation of MobileFaceNet, loaded from the file `mfn_s8_v1.espdl` on the SD card. MobileFaceNet was introduced by Chen et al. (2018) as a class of depthwise-separable convolutional networks specifically optimised for on-device face verification. Unlike standard MobileNets, which use global average pooling at the final layer, MobileFaceNet employs global depthwise convolution to better exploit facial spatial structure before generating the embedding, yielding superior accuracy for an equivalent parameter count.

The model produces a 128-dimensional embedding vector $\mathbf{e} \in \mathbb{R}^{128}$ from the aligned 112 × 112 face image. When trained with angular margin losses such as ArcFace (Deng et al., 2019), the embedding vectors are normalised to lie on a unit hypersphere, and the angular distance between two vectors is a well-calibrated proxy for facial identity similarity.

### 3.6.6 Identity Matching: Cosine Similarity

The recognition decision is made by comparing the query embedding $\mathbf{e}_{query}$ against all stored enrollment embeddings $\{\mathbf{e}_k\}$ using cosine similarity:

$$S(\mathbf{e}_{query}, \mathbf{e}_k) = \frac{\mathbf{e}_{query} \cdot \mathbf{e}_k}{\|\mathbf{e}_{query}\| \cdot \|\mathbf{e}_k\|} \tag{3.4}$$

Cosine similarity was chosen over Euclidean distance because it is invariant to the magnitude (L2 norm) of the embedding vectors. Since different lighting conditions and poses alter the magnitude of the raw output vector, Euclidean distance can be misleadingly large even between genuine same-person pairs captured in different illumination. Cosine similarity, which measures only the angular difference, is more robust to these photometric variations.

The user whose enrollment embedding yields the highest cosine similarity is identified as the candidate. A recognition decision of "present" is made only if the highest similarity score $S_{max}$ exceeds a configured threshold $\tau$:

$$\text{Decision} = \begin{cases} \text{Recognised as } k^{*} & \text{if } S_{max} = \max_k S(\mathbf{e}_{query}, \mathbf{e}_k) \geq \tau \\ \text{Unknown} & \text{otherwise} \end{cases} \tag{3.5}$$

where $k^{*}$ denotes the matched user and $\tau = 0.65$ is the configured threshold. This threshold was calibrated during testing against a sample of twelve participants, as described in Section 3.9.

### 3.6.7 Multi-Frame Enrollment Strategy

During the enrollment of a new user, the system captures 30 video frames in succession and applies the three quality filters (sharpness, brightness, yaw angle). From the frames that pass all filters, the 15 highest-sharpness samples are retained. The MobileFaceNet embedding is extracted for each retained frame, and the final stored template is the arithmetic mean of the 15 individual embedding vectors:

$$\mathbf{e}_{template} = \frac{1}{N} \sum_{i=1}^{N} \mathbf{e}_i, \quad N = 15 \tag{3.6}$$

Averaging multiple embeddings produces a centroid in embedding space that is more representative of the subject's typical appearance than any single sample. This strategy reduces the false-rejection rate during subsequent recognition, particularly for individuals whose facial appearance varies noticeably across the enrollment session due to micro-expressions or minor head movement.

### 3.6.8 Database Design

The local database is a SQLite3 relational database stored on the SD card at `/sdcard/attendance.db`. It consists of four tables:

**`users`** — Records the identity and role of each enrolled person, with fields for UUID (a randomly generated 128-bit identifier formatted as a hex string), full name, student or staff ID, role (`student`, `lecturer`, or `admin`), and phone number. The UUID serves as the primary key and is also the record identifier used when synchronising with the cloud backend.

**`embeddings`** — Stores the 128-dimensional float32 embedding vector for each enrolled user as a binary blob, linked to the `users` table by UUID. Separating embeddings into their own table allows the user record to be read without loading the embedding, which is relevant for the user management screen where only names and IDs need to be displayed.

**`schedules`** — Stores lecture timetable entries pulled from the cloud backend, with fields for course code, course title, start time and end time (as Unix timestamps), and synchronisation status. The recognition pipeline queries this table at the moment of each recognition event to determine which active schedule to associate with the attendance record.

**`attendance_logs`** — Records each recognition event with a UUID, a foreign key linking to the recognised user, a foreign key linking to the active schedule, a Unix timestamp, a status field (`present`), and a `synced` flag that tracks whether the record has been transmitted to the cloud.

The database was designed in third normal form (3NF) to eliminate redundant data and prevent update anomalies. A duplicate attendance guard was implemented as a SQL pre-check that queries whether a record for the same user already exists within the current schedule window before inserting a new one, preventing repeated entries when a student stands in the camera's field of view for an extended period.

All SQLite access is serialised through the `db_task`, which processes requests from `g_db_request_queue` one at a time. This design eliminates concurrent write contention and the associated `SQLITE_BUSY` errors that caused silent data loss in an earlier implementation.

### 3.6.9 Graphical User Interface (LVGL 9)

The user interface was constructed entirely within LVGL 9, targeting the 1024 × 600 display. The interface is organised into five primary screens managed by a central navigation callback:

1. **Main Attendance Screen**: Displays a 480 × 360 pixel live camera preview with a coloured bounding box overlay that tracks the detected face. On recognition, the matched name and a confidence indicator are displayed in a notification panel. This screen is the default view during normal operation.
2. **Setup Wizard**: A multi-step guided screen shown only on first boot, collecting Wi-Fi credentials and performing the initial administrator enrollment. On completion, a flag is written to Non-Volatile Storage (NVS) to prevent the wizard from appearing again.
3. **Enrollment Screen**: Guides an administrator through the process of adding a new user. A progress bar shows the number of frames captured, and a quality indicator labels each frame as accepted or rejected with the reason.
4. **Settings Screen**: Allows configuration of Wi-Fi credentials, cloud endpoint URL, Telegram credentials, and device identity fields (department name, room location). Protected by PIN authentication.
5. **Reports Screen**: Displays a summary of attendance logs from the local database with filtering by date range, and provides a button to trigger an immediate cloud sync.

All LVGL API calls originating from tasks other than the designated display task are wrapped in `ui_acquire()` / `ui_release()` helper functions, which internally call `lvgl_port_lock(200)` and `lvgl_port_unlock()`. The 200 ms timeout ensures that recognition pipeline latency is bounded even when the display is mid-refresh.

### 3.6.10 Cloud Backend Architecture

The cloud backend is a single Python process that concurrently serves a Telegram bot and a FastAPI REST API. The two run in the same asyncio event loop using `asyncio.gather()`, which allows a single thread to handle both Telegram webhook events and incoming HTTP requests from the device without multi-process complexity.

The Telegram bot implements a **conversational state machine** for lecture scheduling. When a lecturer sends `/schedule`, the bot enters a five-step wizard, prompting for course code, course title, date, start time, and end time in sequence. Input validation is applied at each step (e.g., date format `DD/MM/YYYY`, time format `HH:MM` on a 24-hour clock). On completion, the schedule is inserted into the cloud SQLite database (`bot_data.db`) and becomes available to the device on its next synchronisation pull.

Lecturer identity is linked to the system through phone number matching. When a new lecturer sends `/start` and shares their contact card, the bot extracts the phone number, normalises it by retaining only the last nine significant digits (to handle international code formatting variations), and queries the `users` table for a matching record previously uploaded by the device. If a match is found, the lecturer's Telegram ID is stored in their user record and scheduling features are unlocked.

The two REST endpoints exposed by the FastAPI server are:
- `POST /api/sync_users`: Accepts a JSON array of user records uploaded by the device. Each record is upserted into the cloud `users` table using a `INSERT OR REPLACE` strategy keyed on UUID.
- `GET /api/get_schedules?since=<timestamp>`: Returns a JSON array of schedule records created after the specified Unix timestamp, allowing the device to retrieve only new schedules rather than the full history.

The backend is deployed on Render's free web service tier. Render hibernates free containers after 15 minutes of HTTP inactivity, so the bot is configured to operate in **webhook mode** in production: Telegram delivers messages by making HTTP POST requests to the container URL, which wakes the instance as needed. The device communicates with the backend over HTTPS, validating the server's TLS certificate against the CA bundle embedded in the firmware.

---

## 3.7 Implementation Procedure

The system was implemented in the sequential order described below. This order was determined by the dependency structure of the subsystems: each step was verified to be functional before the next was begun.

**Step 1 — Toolchain and Build Environment Setup**
The ESP-IDF v5.3 toolchain was installed and configured. The project structure was established with a `CMakeLists.txt` root, `idf_component.yml` dependency manifest, and initial `sdkconfig.defaults.esp32p4` file. The PSRAM was enabled in `sdkconfig` with HEX mode at 200 MHz (requiring `CONFIG_IDF_EXPERIMENTAL_FEATURES=y`), the flash size was configured to 16 MB, and the custom partition table was defined.

**Step 2 — Board Bringup (LDO, GPIO, Backlight)**
The board initialisation function (`board_init()`) was implemented to configure the LDO regulators that power the display and camera peripherals, set the backlight PWM channel, and initialise the I2C bus on GPIO 45/46 at 100 kHz for the touch controller. This step was verified by observing a measurable voltage on the LDO output rails with a multimeter and confirming that the backlight LED responded to PWM duty changes.

**Step 3 — Display Driver and LVGL Integration**
The MIPI-DSI LCD driver for the EK79007 bridge was implemented and configured with the Elecrow reference lane timing. LVGL 9 was initialised with double frame buffers allocated in PSRAM. The `flush_cb` and `tick_cb` were registered. This step was verified by rendering a solid-colour screen and then a simple LVGL button, confirming that the display controller and graphics pipeline were operational.

**Step 4 — Touch Controller Integration**
The GT911 touch driver was integrated using the manual RST/INT GPIO sequence described in Section 3.6 and Hardware Challenge 1. This step was verified by printing raw touch coordinates to the serial monitor and confirming that they tracked finger position correctly across the full screen area.

**Step 5 — SD Card Mounting and Database Initialisation**
The SDMMC 1-bit driver was configured and the FAT32 filesystem mounted at `/sdcard`. The SQLite3 port was linked and the four-table schema (`users`, `embeddings`, `schedules`, `attendance_logs`) was created on first boot. This step was verified by enrolling a test user record directly through a function call and reading it back from the database.

**Step 6 — Camera and CSI Driver Integration**
The MIPI-CSI camera driver was initialised for the SC2336 sensor at 320 × 240 resolution. The semaphore-callback pipeline was implemented: the CSI DMA completion callback posts a semaphore, and the camera task waits on it before copying the frame and posting it to `g_camera_frame_queue`. This step was verified by rendering the raw camera output to the LVGL display canvas.

**Step 7 — Wi-Fi and Cloud Connectivity**
The `esp_hosted` transport was configured to bridge Wi-Fi functionality from the ESP32-C6 co-processor. The Wi-Fi manager was implemented with credential storage in NVS and automatic reconnection logic. The FastAPI REST API was deployed on the cloud backend. This step was verified by observing a successful HTTPS POST to `/api/sync_users` from the device.

**Step 8 — Face Detection Integration**
The two-stage ESP-DL face detector was initialised and integrated into the recognition task. The three quality metrics (sharpness, brightness, yaw) were implemented and validated by logging their values for a range of face positions and distances.

**Step 9 — Face Alignment Implementation**
The five-landmark similarity transformation was implemented in `face_alignment.c`. The correctness of the aligned output was verified visually by rendering the aligned face crops to the display before passing them to the feature extractor.

**Step 10 — MobileFaceNet Integration and Recognition**
The MobileFaceNet model was loaded from the SD card and the feature extractor was integrated into the recognition pipeline. The cosine similarity matching against the in-memory embedding cache was implemented and the recognition threshold was calibrated. End-to-end recognition was verified: enrolling a test user and confirming that their face was correctly identified in subsequent recognition sessions.

**Step 11 — Cloud Synchronisation and Telegram Bot**
The bi-directional sync logic was completed: the device uploads users, pulls schedules, and transmits CSV attendance files. The Telegram bot was deployed on Render and verified by creating a test schedule and confirming it appeared on the device after a sync.

**Step 12 — System Hardening and Final Integration**
All concurrent access bugs (database serialisation, LVGL mutex enforcement, state machine race conditions, Wi-Fi retry counter exhaustion) were identified and resolved. Security measures (NVS encryption, per-session BLE PIN, mandatory PIN re-authentication for enrollment) were implemented. Battery monitoring and idle power management were configured. A full integration test was performed from device power-on through enrollment, recognition, attendance logging, and cloud synchronisation.

---

## 3.8 Data Collection Procedure

Data collection in this project encompasses both the operational data the system generates (attendance records) and the performance data used to evaluate it (recognition outcomes and system metrics).

### 3.8.1 Biometric Template Collection (Enrollment)

For each person enrolled in the system, the enrollment procedure was executed as follows:
1. The administrator authenticated via PIN and navigated to the Enrollment screen.
2. The subject stood at a fixed distance of approximately 0.5–1.0 metres from the device, facing the camera directly.
3. The enrollment task captured 30 consecutive frames at VGA resolution. Quality filters (sharpness ≥ 50 Laplacian variance units, brightness 40–200 luma units, |yaw| ≤ 25°) were applied to each frame.
4. The 15 highest-quality frames that passed all filters were selected, and MobileFaceNet embeddings were extracted for each.
5. The mean embedding was computed and stored in the SQLite `embeddings` table alongside the user's profile in the `users` table.

During evaluation testing, twelve volunteers (referred to as P01–P12) were enrolled under this procedure. All volunteers provided informed consent and were made aware that their facial templates were stored locally on the device and not transmitted to any external service.

### 3.8.2 Recognition Test Data

For performance evaluation, recognition tests were conducted across three lighting conditions to simulate a realistic classroom environment:

- **Condition A — Standard overhead fluorescent lighting**: Direct illumination from a ceiling-mounted fluorescent tube, approximately 500 lux at the device position.
- **Condition B — Side lighting**: Illumination from a window or lamp positioned 90° to the subject's left, creating mild shadowing on the right side of the face.
- **Condition C — Mixed/backlit**: A combination of overhead and ambient natural light, creating varying exposure across the face.

Under each condition, each of the twelve enrolled participants performed five recognition attempts, yielding 180 genuine-pair test samples. Additionally, each participant attempted to be recognised as each of the eleven other participants (non-self attempts), yielding 660 impostor-pair test samples. The results were recorded in a log file on the SD card.

### 3.8.3 System Performance Data

Alongside recognition outcomes, the following system metrics were logged during testing:

- **End-to-end recognition latency**: Time from frame arrival at the recognition task to completion of the recognition decision, measured in milliseconds using `esp_timer_get_time()`.
- **Memory usage**: Peak heap usage in internal SRAM and PSRAM, recorded using `heap_caps_get_largest_free_block()` at one-second intervals during a 30-minute recognition session.
- **Synchronisation duration**: Time elapsed from initiation of a cloud sync cycle to completion, recorded across ten sync events.
- **Battery discharge curve**: Battery voltage readings logged every 10 seconds during a 4-hour continuous operation test, used to estimate operational battery life.
- **SD card storage utilisation**: Database file size recorded after every 100 attendance events to characterise storage growth rate.

---

## 3.9 Data Analysis

### 3.9.1 Recognition Performance Metrics

The recognition system was evaluated using the standard biometric performance metrics defined in the ISO/IEC 19795-1 standard. These metrics characterise the trade-off between security (rejecting impostors) and convenience (accepting genuine users).

**True Positive (TP)**: A genuine user is correctly recognised and admitted.
**False Positive (FP)**: An impostor (a different person) is incorrectly identified as a registered user.
**True Negative (TN)**: An impostor is correctly rejected as unknown.
**False Negative (FN)**: A genuine user is incorrectly rejected as unknown.

**Recognition Accuracy:**
$$\text{Accuracy} = \frac{TP + TN}{TP + TN + FP + FN} \times 100\% \tag{3.7}$$

**Precision** (of the positive, "recognised" class):
$$\text{Precision} = \frac{TP}{TP + FP} \tag{3.8}$$

**Recall** (also known as the True Acceptance Rate, TAR):
$$\text{Recall} = \frac{TP}{TP + FN} \tag{3.9}$$

**F₁ Score** (harmonic mean of precision and recall):
$$F_1 = 2 \times \frac{\text{Precision} \times \text{Recall}}{\text{Precision} + \text{Recall}} \tag{3.10}$$

**False Acceptance Rate (FAR)**: The proportion of impostor attempts that are incorrectly accepted. This is the primary security metric.
$$FAR = \frac{FP}{FP + TN} \tag{3.11}$$

**False Rejection Rate (FRR)**: The proportion of genuine attempts that are incorrectly rejected. This is the primary usability metric.
$$FRR = \frac{FN}{FN + TP} \tag{3.12}$$

**Equal Error Rate (EER)**: The threshold value $\tau$ at which FAR = FRR. The EER provides a single threshold-independent figure of merit for comparison between systems.

The recognition threshold $\tau = 0.65$ was selected by plotting the FAR and FRR curves as a function of threshold over the evaluation dataset and identifying the operating point that minimised FRR while holding FAR at zero across all 660 impostor test samples, prioritising security over convenience in the context of academic integrity.

### 3.9.2 Latency and Throughput Analysis

Recognition latency was characterised by its mean ($\bar{t}$), standard deviation ($\sigma_t$), and 95th percentile ($t_{95}$) across all successful recognition events during the 30-minute test session:

$$\bar{t} = \frac{1}{N}\sum_{i=1}^{N} t_i \tag{3.13}$$

$$\sigma_t = \sqrt{\frac{1}{N}\sum_{i=1}^{N}(t_i - \bar{t})^2} \tag{3.14}$$

The 95th percentile latency is particularly important from a user experience perspective: it characterises the worst-case delay experienced by 95% of users, which is the value most relevant to practical deployment.

Effective throughput (persons per minute the system can process) was computed from the mean latency:

$$\text{Throughput} = \frac{60{,}000 \text{ ms/min}}{\bar{t} \text{ ms/recognition}} \tag{3.15}$$

### 3.9.3 Resource Utilisation Analysis

Memory utilisation was reported as the fraction of total available capacity consumed under peak load:

$$\text{Memory Utilisation (\%)} = \frac{\text{Peak Used (bytes)}}{\text{Total Available (bytes)}} \times 100\% \tag{3.16}$$

Battery life was estimated by fitting a linear regression to the logged discharge curve and extrapolating to the minimum operational voltage (3.3 V). Storage growth rate was characterised as bytes per attendance event, enabling a projection of SD card lifetime at typical usage levels.

---

## 3.10 System Testing

Testing was carried out at four levels, progressing from individual component verification to full end-to-end evaluation.

### 3.10.1 Unit Testing

Each software module was tested in isolation before integration. The face detector was tested by feeding it a set of reference images and verifying that bounding boxes and landmark coordinates were within expected tolerances. The alignment module was tested by applying the transformation to a face image with known landmark positions and measuring the pixel-level error at the output. The cosine similarity function was tested against analytically computed reference values. The database module was tested with a sequence of insert, query, and delete operations and the results verified against expected SQL query outputs.

### 3.10.2 Integration Testing

After unit testing, subsystems were combined progressively and the integration boundaries were tested. Camera-to-detection integration was verified by confirming that bounding boxes rendered on the live preview correctly tracked a moving face. Detection-to-recognition integration was verified by confirming that only enrolled users triggered a positive recognition result. Recognition-to-database integration was verified by confirming that every positive recognition event produced exactly one database row within a configurable deduplication window.

### 3.10.3 Field Testing

Field testing was conducted in the intended deployment environment: a university computer laboratory. The device was operated for a full 90-minute class session with 25 students entering and being recognised at the door. Recognition outcomes were logged and compared against manual attendance recorded simultaneously by an observer. This test measured accuracy under real-world conditions (variable lighting, glasses, face masks partially removed, students approaching from various angles) rather than the controlled conditions of the laboratory evaluation.

### 3.10.4 Stress and Endurance Testing

A stress test was conducted by running the recognition loop continuously for 30 minutes without interruption, monitoring for memory leaks (through periodic `heap_caps_get_info()` calls), watchdog events, and task stack high-water marks. An endurance test ran the device on battery power for four hours, verifying that attendance logging continued reliably throughout and that the battery monitoring correctly triggered a low-battery warning before the voltage dropped below the safe operating floor.

### 3.10.5 Offline Resilience Testing

The cloud synchronisation and local fallback behaviour were tested by deliberately disconnecting the device from Wi-Fi before a class session and verifying that attendance logging continued uninterrupted to the local database. After the session, Wi-Fi was restored and sync was triggered manually, confirming that all records accumulated during the offline period were successfully uploaded.

---

## 3.11 Ethical and Privacy Considerations

The SmartAttendance system captures and processes biometric data — specifically, facial images and the derived mathematical embedding vectors used for identity recognition. Such data falls under the definition of sensitive personal information in most data protection frameworks, including the Nigerian Data Protection Regulation (NDPR, 2019). The following measures were incorporated into the system design to ensure responsible data handling.

**Informed Consent**: All participants enrolled in the system during development and evaluation were briefed on the nature and purpose of the data collection and provided verbal consent. No facial data was collected from any individual without prior awareness and agreement.

**Local Biometric Storage**: Facial embedding vectors are stored exclusively on the SD card of the physical device. Embedding vectors are never transmitted to the cloud backend; only non-biometric profile fields (name, student ID, phone number, and role) are synchronised to the cloud server. This architectural decision means that the biometric templates can never be intercepted in transit or exposed through a cloud server breach.

**No Facial Image Retention**: The system does not store captured facial images at any point. The image pixels are used transiently during recognition processing and are immediately discarded when the recognition decision is complete. Only the derived embedding vector — which cannot be reverse-engineered to reconstruct a recognisable facial image — is persisted.

**Access Control for Enrollment**: New user enrollment requires administrator PIN authentication before the enrollment process can begin. This prevents unauthorised self-enrollment and ensures that all persons in the database have been explicitly approved by an administrator.

**Data Minimisation**: The system collects only the fields strictly necessary for attendance management. No biometric data beyond the embedding vector is stored, and no attendance records are linked to any personally identifiable information beyond the user's pre-registered name and student ID.

---

## 3.12 Chapter Summary

This chapter has presented the complete research design, materials, and methodological framework of the SmartAttendance project. An iterative prototyping approach was adopted and justified as the most appropriate methodology for embedded AI development on a novel hardware platform with incompletely documented peripheral behaviour. The hardware selection — centred on the ESP32-P4 with its native MIPI-CSI/DSI interfaces and 32 MB HEX-mode PSRAM — was driven by the requirement to execute all biometric inference locally, without cloud dependency. The software architecture was designed around a FreeRTOS task model with typed inter-task queues and a global state machine, ensuring that concurrent subsystems do not interfere with one another. The AI recognition pipeline implements a four-stage process — detection, geometric alignment, MobileFaceNet embedding, and cosine similarity matching — drawn from methods established in the face recognition literature and adapted for integer-quantised on-device deployment. The cloud backend provides lecturer scheduling and data synchronisation through a Telegram bot and FastAPI REST API, deployed on a cost-free cloud hosting platform. Performance evaluation will be conducted using the standard biometric metrics of accuracy, precision, recall, F₁, FAR, and FRR, alongside latency, throughput, memory utilisation, battery life, and storage growth rate. Chapter Four will present the results obtained from applying these procedures, and Chapter Five will interpret those results in relation to the project's objectives.

---

## References

Barry, R., & Hyett, T. (2017). *Mastering the FreeRTOS real-time kernel: A hands-on tutorial guide*. Real Time Engineers Ltd. https://www.freertos.org/Documentation/RTOS_book.html

Berger, E. (2017). *Making embedded systems: Design patterns for great software* (2nd ed.). O'Reilly Media.

Chen, S., Liu, Y., Gao, X., & Han, Z. (2018). MobileFaceNets: Efficient CNNs for accurate real-time face verification on mobile devices. In *Proceedings of the 13th Chinese Conference on Biometric Recognition (CCBR 2018)*, Lecture Notes in Computer Science, vol. 10996. Springer. https://arxiv.org/abs/1804.07573

Creswell, J. W., & Creswell, J. D. (2018). *Research design: Qualitative, quantitative, and mixed methods approaches* (5th ed.). SAGE Publications.

Deng, J., Guo, J., Xue, N., & Zafeiriou, S. (2019). ArcFace: Additive angular margin loss for deep face recognition. In *Proceedings of the IEEE/CVF Conference on Computer Vision and Pattern Recognition (CVPR)*, 4690–4699. https://doi.org/10.1109/CVPR.2019.00482

Espressif Systems. (2024). *ESP-IDF programming guide (v5.3)*. https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32p4/

ISO/IEC 19795-1:2021. (2021). *Information technology — Biometric performance testing and reporting — Part 1: Principles and framework*. International Organization for Standardization.

LVGL. (2024). *LVGL — Light and versatile graphics library* (Version 9). https://lvgl.io

National Information Technology Development Agency (NITDA). (2019). *Nigeria Data Protection Regulation (NDPR)*. Federal Government of Nigeria.

Zhang, K., Zhang, Z., Li, Z., & Qiao, Y. (2016). Joint face detection and alignment using multitask cascaded convolutional networks. *IEEE Signal Processing Letters*, *23*(10), 1499–1503. https://doi.org/10.1109/LSP.2016.2603342
