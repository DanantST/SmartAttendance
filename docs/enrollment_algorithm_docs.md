# Statistically Robust Representative Facial Template Enrollment Algorithm

## 1. Executive Summary

This document describes the 8-step statistically robust facial template enrollment algorithm designed and implemented for the **SmartAttendance** edge biometric system on the **Elecrow CrowPanel Advanced 7" ESP32-P4** board.

Rather than relying on single-frame capture or simple unweighted averaging, the system captures 30 valid facial frames, applies adaptive outlier filtering ($\mu - 1.5\sigma$, clamped to $\ge 0.65$), performs K-Means pose clustering ($K=3$ with automatic fallback for non-empty clusters), and synthesizes a single, highly discriminative representative embedding weighted by cluster size.

---

## 2. Mathematical Formulation & Step-by-Step Architecture

### Step 1: Sample Acquisition & Strict Quality Filtering
The device captures facial frames until $N = 30$ valid samples are collected. A frame is rejected immediately if:
- Detected face count $\neq 1$
- Detection score $< \text{FACE\_DETECT\_CONFIDENCE\_MIN}$ ($0.60$)
- Bounding box dimension $< \text{FACE\_MIN\_SIZE\_PX}$ ($50\text{px}$)
- Sharpness (Laplacian variance) $< \text{ENROLL\_SHARPNESS\_MIN}$ ($50.0$)
- Luminance $\notin [40, 200]$
- Head pose yaw $|\hat{\theta}_{yaw}| > 25.0^\circ$

Rejected attempts are logged as `rejected_count` and live progress (`Capturing sample X / 30`) is pushed to the LVGL UI.

### Step 2: Temporary PSRAM Embedding Storage
For each valid sample $i \in \{1 \dots 30\}$, the face crop is aligned to a canonical $112 \times 112$ RGB image and processed by MobileFaceNet int8 feature extractor to yield a 128-dimensional embedding $\mathbf{e}_i \in \{-128, \dots, 127\}^{128}$.
All 30 embeddings are stored temporarily in a PSRAM-backed buffer array (`embeddings[30]`), avoiding internal SRAM stack overflow or heap fragmentation.

### Step 3: Outlier Removal via Adaptive Threshold ($\mu - 1.5\sigma$)
To remove poor embeddings caused by partial blinking, transient glare, or micro-movement:
1. Compute the $30 \times 30$ pairwise cosine similarity matrix $S_{i,j} = \text{cos\_sim}(\mathbf{e}_i, \mathbf{e}_j)$.
2. Compute the mean inter-sample similarity $m_i$ for each sample:
   $$m_i = \frac{1}{29} \sum_{j \neq i} S_{i,j}$$
3. Compute the grand mean $\mu$ and standard deviation $\sigma$ across all $m_i$:
   $$\mu = \frac{1}{30}\sum_{i=1}^{30} m_i, \quad \sigma = \sqrt{\frac{1}{30}\sum_{i=1}^{30} (m_i - \mu)^2}$$
4. Establish adaptive outlier threshold:
   $$T_{outlier} = \max(\mu - 1.5\sigma, \, 0.65)$$
5. Any sample with $m_i < T_{outlier}$ is marked as an outlier and discarded. The remaining $M \le 30$ samples form the inlier set $\mathcal{I}$.

### Step 4 & 5: K-Means Pose Clustering ($K=3$ with Fallback)
Natural face captures contain pose variations (frontal, slight left, slight right).
1. Set target cluster count $K = \min(3, M)$.
2. Run 10 iterations of K-Means clustering using cosine similarity on the $M$ inlier embeddings.
3. Compute cluster centroids $C_k$ for each cluster $k$.
4. **Automatic Fallback**: Empty clusters ($|S_k| = 0$) are pruned. The system retains only the $K_{effective}$ non-empty clusters with centroids $C_1 \dots C_{K_{effective}}$ and cluster sizes $|S_1| \dots |S_{K_{effective}}|$.

### Step 6: Cluster-Size Weighted Averaging & Norm Restoration
The representative template is synthesized by weighting each centroid by its cluster size:
$$\mathbf{v}_{repr} = \frac{\sum_{k=1}^{K_{effective}} |S_k| \cdot C_k}{\sum_{k=1}^{K_{effective}} |S_k|}$$

To preserve feature vector magnitude:
1. Compute target norm $\bar{\rho}$ as the mean L2 norm of all $M$ inlier embeddings:
   $$\bar{\rho} = \frac{1}{M} \sum_{i \in \mathcal{I}} \|\mathbf{e}_i\|_2$$
2. Scale $\mathbf{v}_{repr}$ to $\bar{\rho}$ and quantise values to int8 [-128, 127] to produce $\mathbf{e}_{final}$.

### Step 7: Quality Scoring & Database Persistence
An enrollment quality score $Q \in [0, 100]$ is computed as:
$$Q = \text{round}\left(0.40 \cdot R_{inlier} + 0.40 \cdot C_{compact} + 0.20 \cdot S_{sharp}\right)$$
- $R_{inlier} = \frac{M}{30} \times 100$
- $C_{compact} = \text{clamp}\left(\frac{\bar{m}_{inliers} - 0.65}{0.35} \times 100, \, 0, \, 100\right)$
- $S_{sharp} = \text{clamp}\left(\frac{\text{mean}(sharpness)}{120} \times 100, \, 0, \, 100\right)$

**Rating Mapping**:
- $Q \ge 85 \implies$ `EXCELLENT`
- $70 \le Q < 85 \implies$ `GOOD`
- $55 \le Q < 70 \implies$ `AVERAGE`
- $Q < 55 \implies$ `POOR`

**Database Storage**:
ONLY the single final representative embedding $\mathbf{e}_{final}$ is stored in the SQLite `users` table, along with metadata: `created_at`, `enroll_quality`, `enroll_accepted`, `enroll_rejected`, `model_version` (1), and `embedding_dim` (128).

### Step 8: Recognition & UI Quality Feedback
The live recognition engine compares query embeddings against $\mathbf{e}_{final}$ using cosine similarity threshold $\tau = 0.65$.
On enrollment completion, the UI displays the quality score card. The **Redo Enrollment** action button is displayed **ONLY when the quality rating is `POOR`**.

---

## 3. Database Schema Reference

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT UNIQUE NOT NULL,
    name TEXT NOT NULL,
    student_id TEXT UNIQUE,
    phone_number TEXT,
    telegram_id TEXT,
    role TEXT NOT NULL CHECK(role IN ('student','lecturer','admin')),
    face_embedding BLOB,
    created_at INTEGER NOT NULL,
    updated_at INTEGER,
    enroll_quality INTEGER DEFAULT 0,
    enroll_accepted INTEGER DEFAULT 0,
    enroll_rejected INTEGER DEFAULT 0,
    model_version INTEGER DEFAULT 1,
    embedding_dim INTEGER DEFAULT 128
);
```

---

## 4. Memory & Performance Characteristics

- **Memory Overhead**: All enrollment buffers (frames, aligned face crops, embeddings, quality scores, sharpness arrays) are allocated in PSRAM via `heap_caps_calloc(..., MALLOC_CAP_SPIRAM)` and freed in the `cleanup:` block. Zero dynamic heap fragmentation.
- **Latency**: Outlier removal + K-Means clustering + weighted synthesis takes $< 15\text{ms}$ total on the ESP32-P4 at 400 MHz.
