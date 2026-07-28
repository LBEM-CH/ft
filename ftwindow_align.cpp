#include "ftwindow_common.h"

#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <limits>

// ---------------------------------------------------------------------------
//  Align image to reference
//
//  Two ways to move an image onto a reference image held in another buffer:
//
//    * Shift align    — cross-correlates the two through the Fourier domain,
//                       takes the position of the correlation maximum as the
//                       displacement, and shifts the image cyclically by it.
//    * Rotation align — turns the image through a full circle in 0.5° steps
//                       and keeps the orientation whose correlation with the
//                       reference is highest.
//
//  Neither touches the reference; the result goes to the chosen output buffer,
//  which then becomes the displayed one.
// ---------------------------------------------------------------------------

namespace {

// Clearing the item's enabled flag stops it being picked. The black is set
// explicitly because the combo's stylesheet deliberately carries no `color` for
// the popup: a colour there is applied by QStyleSheetStyle to every row alike,
// and would hide the greyed-out one. Note this data only reaches *enabled*
// rows — Qt takes a disabled row's text colour from the palette's Disabled
// group instead, which is why the delegate below exists.
void setComboItemEnabled(QComboBox *cb, int idx, bool on)
{
    auto *model = qobject_cast<QStandardItemModel *>(cb->model());
    if (!model) return;
    QStandardItem *item = model->item(idx);
    if (!item) return;
    Qt::ItemFlags f = item->flags();
    item->setFlags(on ? (f |  (Qt::ItemIsEnabled | Qt::ItemIsSelectable))
                      : (f & ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable)));
    cb->setItemData(idx, QColor(Qt::black), Qt::ForegroundRole);
}

// Paints the one unusable entry grey. Neither of the two obvious routes works
// once the combo has a stylesheet, as every parameter window's does: the item's
// Qt::ForegroundRole is only consulted for enabled rows, and a disabled row's
// palette colour is overridden by QStyleSheetStyle. Handing the row to the
// style and overpainting the label does not work either — the style draws the
// text straight from the model, leaving black underneath — so a disabled row is
// drawn here from scratch. It has no hover or selection state to render anyway,
// being unselectable.
class DisabledGreyDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &index) const override
    {
        if (index.flags() & Qt::ItemIsEnabled) {
            QStyledItemDelegate::paint(p, opt, index);
            return;
        }
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, index);
        const QWidget *w = o.widget;
        QStyle *st = w ? w->style() : QApplication::style();
        QRect textRect = st->subElementRect(QStyle::SE_ItemViewItemText, &o, w);

        p->save();
        p->fillRect(o.rect, Qt::white);   // matches the popup's stylesheet background
        p->setFont(o.font);
        p->setPen(QColor(0xaa, 0xaa, 0xaa));
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, o.text);
        p->restore();
    }
};

// Bilinear sample of a w×h field. Positions outside the field return `outside`,
// which lets a rotation fill the corners it sweeps in with the image mean
// instead of a black step.
double sampleBilinear(const std::vector<double> &v, int w, int h,
                      double x, double y, double outside)
{
    if (x < 0.0 || y < 0.0 || x > w - 1.0 || y > h - 1.0) return outside;
    int x0 = (int)x, y0 = (int)y;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    double fx = x - x0, fy = y - y0;
    return v[(size_t)y0 * w + x0] * (1 - fx) * (1 - fy)
         + v[(size_t)y0 * w + x1] *      fx  * (1 - fy)
         + v[(size_t)y1 * w + x0] * (1 - fx) *      fy
         + v[(size_t)y1 * w + x1] *      fx  *      fy;
}

double meanOf(const std::vector<double> &v)
{
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double d : v) s += d;
    return s / v.size();
}

// Resample the largest centred square of a w×h image onto an S×S grid. Both
// inputs of the rotation search go through this, so images of different sizes
// (or aspect ratios) are compared on a common grid.
std::vector<double> centredSquareToGrid(const std::vector<double> &v,
                                        int w, int h, int S)
{
    double side = std::min(w, h);
    double x0 = (w - side) / 2.0, y0 = (h - side) / 2.0;
    double step = (S > 1) ? (side - 1.0) / (S - 1.0) : 0.0;
    double mean = meanOf(v);
    std::vector<double> out((size_t)S * S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
            out[(size_t)y * S + x] =
                sampleBilinear(v, w, h, x0 + x * step, y0 + y * step, mean);
    return out;
}

} // namespace

// Must be re-run after every setStyleSheet() on the combo: restyling makes
// QComboBox reinstall a delegate of its own choosing, dropping ours.
void FtWindow::styleAlignComboPopup(QComboBox *cb)
{
    if (!m_alignItemDelegate)
        m_alignItemDelegate = new DisabledGreyDelegate(this);
    cb->view()->setItemDelegate(m_alignItemDelegate);
}

std::vector<double> FtWindow::alignSlotPixels(int idx, int &w, int &h) const
{
    w = 0; h = 0;
    if (idx < 0 || idx >= HISTORY_SLOTS) return {};

    QImage img;
    std::vector<double> pix;
    if (idx == m_activeSlot && !m_image.isNull()) {
        img = m_image;
        pix = m_imageRawPixels;
    } else if (m_history[idx].occupied) {
        img = m_history[idx].image;
        pix = m_history[idx].rawPixels;
    } else {
        return {};
    }
    if (img.isNull()) return {};

    w = img.width();
    h = img.height();
    // A raw-pixel array out of step with the image (possible for slots restored
    // from an older session) is rebuilt from the 8-bit image rather than used.
    if ((int)pix.size() != w * h) {
        QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
        pix.assign((size_t)w * h, 0.0);
        for (int y = 0; y < h; y++) {
            const uchar *row = gray.constScanLine(y);
            for (int x = 0; x < w; x++)
                pix[(size_t)y * w + x] = row[x];
        }
    }
    return pix;
}

// The file a buffer was loaded from, and its pixel size. Both read the live
// values when the buffer is the active one, matching alignSlotPixels().
QString FtWindow::alignSlotPath(int idx) const
{
    if (idx < 0 || idx >= HISTORY_SLOTS) return QString();
    if (idx == m_activeSlot && !m_image.isNull()) return m_imagePath;
    return m_history[idx].path;
}

double FtWindow::alignSlotPixelSize(int idx) const
{
    if (idx < 0 || idx >= HISTORY_SLOTS) return 1.0;
    if (idx == m_activeSlot && !m_image.isNull()) return m_pixelSize;
    return m_history[idx].pixelSize;
}

bool FtWindow::alignInputsValid() const
{
    if (!m_alignSrcCombo || !m_alignRefCombo) return false;
    int src = m_alignSrcCombo->currentIndex();
    int ref = m_alignRefCombo->currentIndex();
    if (src < 0 || ref < 0) return false;
    return src != ref;
}

void FtWindow::syncAlignCombos()
{
    if (!m_alignRefCombo) return;
    int src = m_alignSrcCombo->currentIndex();
    for (int i = 0; i < HISTORY_SLOTS; i++)
        setComboItemEnabled(m_alignRefCombo, i, i != src);

    // An image cannot serve as its own reference, so when the two selectors
    // coincide the alignment actions are switched off along with the greyed-out
    // list entry. Cancel stays live — it is the way out of the window.
    bool ok = alignInputsValid();
    m_alignShiftBtn->setEnabled(ok);
    m_alignRotBtn->setEnabled(ok);
    update();
}

void FtWindow::onAlignCancel()
{
    m_alignActive = false;
    m_alignResult.clear();
    m_alignSrcCombo->hide();
    m_alignRefCombo->hide();
    m_alignOutCombo->hide();
    m_alignCancelBtn->hide();
    m_alignShiftBtn->hide();
    m_alignRotBtn->hide();
    update();
}

// ---------------------------------------------------------------------------
//  Writing the result
// ---------------------------------------------------------------------------
// Store `result` (w×h raw values) in buffer `outIdx` and display it. The buffer
// currently on display is written back to its own slot first, so live edits are
// not lost when the display moves to the output buffer.
//
// `sourcePath` is the file the *source* buffer came from, and the output buffer
// inherits it verbatim: an aligned image is still that file, only moved, so
// "Reload image" must go on fetching the original from disk. Writing a
// description of the operation here instead would break reload and would also
// cost the buffer its place in the saved session, which stores paths.
void FtWindow::finishAlign(int outIdx, std::vector<double> result,
                           int w, int h, double pixelSize, const QString &sourcePath)
{
    if (result.empty() || (int)result.size() != w * h) return;
    double mn = result[0], mx = result[0];
    for (double v : result) { mn = std::min(mn, v); mx = std::max(mx, v); }
    double range = mx - mn;
    double scale = (range > 0) ? 255.0 / range : 1.0;

    QImage outImg(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; y++) {
        uchar *row = outImg.scanLine(y);
        for (int x = 0; x < w; x++)
            row[x] = static_cast<uchar>(std::clamp(
                (result[(size_t)y * w + x] - mn) * scale, 0.0, 255.0));
    }

    if (m_activeSlot >= 0 && !m_image.isNull()) {
        m_history[m_activeSlot].image     = m_image;
        m_history[m_activeSlot].path      = m_imagePath;
        m_history[m_activeSlot].rawPixels = m_imageRawPixels;
        m_history[m_activeSlot].minVal    = m_imageMinVal;
        m_history[m_activeSlot].maxVal    = m_imageMaxVal;
        m_history[m_activeSlot].pixelSize = m_pixelSize;
        m_history[m_activeSlot].occupied  = true;
    }

    m_history[outIdx].image        = outImg;
    m_history[outIdx].path         = sourcePath;
    m_history[outIdx].rawPixels    = std::move(result);
    m_history[outIdx].minVal       = mn;
    m_history[outIdx].maxVal       = mx;
    m_history[outIdx].pixelSize    = pixelSize;
    m_history[outIdx].powerSpecImg = computePowerSpecMasked(outImg);
    m_history[outIdx].occupied     = true;
    m_history[outIdx].ftComputed   = false;
    m_history[outIdx].fftData.clear();

    m_activeSlot     = outIdx;
    m_image          = m_history[outIdx].image;
    m_imagePath      = m_history[outIdx].path;
    m_imageRawPixels = m_history[outIdx].rawPixels;
    m_imageMinVal    = mn;
    m_imageMaxVal    = mx;
    if (!m_imageContrastLocked) {
        m_imageDispMin = mn;
        m_imageDispMax = mx;
    }
    m_pixelSize = m_history[outIdx].pixelSize;
    m_zoom[0].reset(w, h);

    m_ftComputed = false;
    m_modeBtn->hide();

    saveHistory();
}

// ---------------------------------------------------------------------------
//  Shift align
// ---------------------------------------------------------------------------
void FtWindow::onAlignShift()
{
    if (!ensureCalcHeadroom(tr("align the image by shifting"))) return;
    onAlignShiftImpl();
}

void FtWindow::onAlignShiftImpl()
{
    if (!alignInputsValid()) return;
    int srcIdx = m_alignSrcCombo->currentIndex();
    int refIdx = m_alignRefCombo->currentIndex();
    int outIdx = m_alignOutCombo->currentIndex();

    int w = 0, h = 0, rw = 0, rh = 0;
    std::vector<double> src = alignSlotPixels(srcIdx, w, h);
    std::vector<double> ref = alignSlotPixels(refIdx, rw, rh);
    if (src.empty() || ref.empty()) {
        m_alignResult = "Source and reference must both hold an image";
        update();
        return;
    }

    storeUndoSnapshot();
    m_alignResult.clear();
    m_toolProgress = 0.05;
    update();

    // The images travel in the shared state rather than in the step lambdas, so
    // each can be released the moment it has been folded into the FFT arrays;
    // a lambda capture would keep it alive until the whole chain is destroyed,
    // and these are the largest allocations the tool makes.
    struct Work {
        std::vector<Complex> fa, fb;
        std::vector<double>  src, ref;
        int N = 0, w = 0, h = 0, rw = 0, rh = 0;
        int srcIdx = 0, refIdx = 0, outIdx = 0;
        double pixelSize = 1.0;
        QString srcPath;
    };
    auto st = std::make_shared<Work>();
    st->src = std::move(src);
    st->ref = std::move(ref);
    st->w = w; st->h = h; st->rw = rw; st->rh = rh;
    st->srcIdx = srcIdx; st->refIdx = refIdx; st->outIdx = outIdx;
    st->pixelSize = alignSlotPixelSize(srcIdx);
    st->srcPath   = alignSlotPath(srcIdx);

    // Both images are mean-subtracted and dropped into the same zero-padded
    // square grid. Mean subtraction keeps a bright background from swamping the
    // correlation; the padding is what lets differently sized images (a small
    // template against a large field, say) still be correlated.
    int N = nextGoodFFTSize(std::max({w, h, rw, rh}));
    st->N = N;

    chainSteps({
        [this, st]() {
            const int N = st->N;
            double mSrc = meanOf(st->src);
            double mRef = meanOf(st->ref);
            st->fa.assign((size_t)N * N, Complex(0, 0));
            st->fb.assign((size_t)N * N, Complex(0, 0));
            for (int y = 0; y < st->h; y++)
                for (int x = 0; x < st->w; x++)
                    st->fa[(size_t)y * N + x] = Complex(st->src[(size_t)y * st->w + x] - mSrc, 0);
            for (int y = 0; y < st->rh; y++)
                for (int x = 0; x < st->rw; x++)
                    st->fb[(size_t)y * N + x] = Complex(st->ref[(size_t)y * st->rw + x] - mRef, 0);
            st->ref.clear();
            st->ref.shrink_to_fit();
            m_toolProgress = 0.2;
        },
        [this, st]() {
            fft2d(st->fa, st->N, false);
            m_toolProgress = 0.45;
        },
        [this, st]() {
            fft2d(st->fb, st->N, false);
            m_toolProgress = 0.7;
        },
        [this, st]() {
            const size_t n = st->fa.size();
            for (size_t i = 0; i < n; i++)
                st->fa[i] *= std::conj(st->fb[i]);
            st->fb.clear();
            fft2d(st->fa, st->N, true);
            m_toolProgress = 0.9;
        },
        [this, st]() {
            const int N = st->N;
            // c[k] = Σ src[n+k]·ref[n], so the peak index k is exactly the
            // amount by which the source has to be read ahead to land on the
            // reference. Indices past the half-grid are the negative shifts.
            int px = 0, py = 0;
            double best = -std::numeric_limits<double>::infinity();
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    double v = st->fa[(size_t)y * N + x].real();
                    if (v > best) { best = v; px = x; py = y; }
                }
            st->fa.clear();
            int kx = (px > N / 2) ? px - N : px;
            int ky = (py > N / 2) ? py - N : py;

            const int w = st->w, h = st->h;
            std::vector<double> outPix((size_t)w * h);
            for (int y = 0; y < h; y++) {
                int sy = ((y + ky) % h + h) % h;
                for (int x = 0; x < w; x++) {
                    int sx = ((x + kx) % w + w) % w;
                    outPix[(size_t)y * w + x] = st->src[(size_t)sy * w + sx];
                }
            }
            st->src.clear();

            finishAlign(st->outIdx, std::move(outPix), w, h,
                        st->pixelSize, st->srcPath);
            // Reported the way a user reads it: how far the image moved, not
            // the index it was read from.
            m_alignResult = QString("Shifted by x = %1, y = %2 pixels").arg(-kx).arg(-ky);
            m_toolProgress = -1;
        }
    });
}

// ---------------------------------------------------------------------------
//  Rotation align
// ---------------------------------------------------------------------------
void FtWindow::onAlignRotate()
{
    if (!ensureCalcHeadroom(tr("align the image by rotating"))) return;
    onAlignRotateImpl();
}

void FtWindow::onAlignRotateImpl()
{
    if (!alignInputsValid()) return;
    int srcIdx = m_alignSrcCombo->currentIndex();
    int refIdx = m_alignRefCombo->currentIndex();
    int outIdx = m_alignOutCombo->currentIndex();

    int w = 0, h = 0, rw = 0, rh = 0;
    std::vector<double> src = alignSlotPixels(srcIdx, w, h);
    std::vector<double> ref = alignSlotPixels(refIdx, rw, rh);
    if (src.empty() || ref.empty()) {
        m_alignResult = "Source and reference must both hold an image";
        update();
        return;
    }

    storeUndoSnapshot();
    m_alignResult.clear();
    m_toolProgress = 0.02;
    update();

    // The 720 trial orientations are scored on a square working grid capped at
    // 512 px rather than on the full image: a whole extra rotation of a large
    // image per half-degree would cost minutes, while at 512 px a half-degree
    // still moves the outermost pixels by more than two pixels, so the angle it
    // finds is the same one. Only the winning angle is then applied at full
    // resolution.
    const int kMaxWork = 512;
    int S = std::min({kMaxWork, std::min(w, h), std::min(rw, rh)});
    if (S < 16) S = std::min({std::min(w, h), std::min(rw, rh)});
    if (S < 4) {
        m_toolProgress = -1;
        m_alignResult = "Images are too small to align";
        update();
        return;
    }

    // As in the shift path, the full-size images live in the shared state so the
    // reference can be dropped once it has been resampled onto the working grid.
    struct Work {
        std::vector<double> srcFull, ref, srcWork, refWork;
        std::vector<int>    diskIdx;      // pixels inside the inscribed circle
        double refNorm = 0.0;
        double srcMean = 0.0;
        int    S = 0, w = 0, h = 0, rw = 0, rh = 0;
        int    srcIdx = 0, refIdx = 0, outIdx = 0;
        double pixelSize = 1.0;
        QString srcPath;
        double bestScore = -std::numeric_limits<double>::infinity();
        double bestAngle = 0.0;
    };
    auto st = std::make_shared<Work>();
    st->S = S; st->w = w; st->h = h; st->rw = rw; st->rh = rh;
    st->srcIdx = srcIdx; st->refIdx = refIdx; st->outIdx = outIdx;
    st->pixelSize = alignSlotPixelSize(srcIdx);
    st->srcPath   = alignSlotPath(srcIdx);
    st->srcMean = meanOf(src);
    st->srcFull = std::move(src);
    st->ref     = std::move(ref);

    const double kStepDeg = 0.5;
    const int    kAngles  = (int)std::lround(360.0 / kStepDeg);   // 720
    const int    kChunks  = 12;

    std::vector<std::function<void()>> steps;

    // Preparation: resample both images onto the working grid, restrict the
    // comparison to the inscribed disk (the only region every rotation keeps
    // inside the frame), and zero-mean the reference over that disk.
    steps.push_back([this, st]() {
        const int S = st->S;
        st->srcWork = centredSquareToGrid(st->srcFull, st->w, st->h, S);
        st->refWork = centredSquareToGrid(st->ref, st->rw, st->rh, S);
        st->ref.clear();
        st->ref.shrink_to_fit();

        double c = (S - 1) / 2.0;
        double rad = S / 2.0 - 1.0;
        double rad2 = rad * rad;
        st->diskIdx.clear();
        st->diskIdx.reserve((size_t)(M_PI * rad2));
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                double dx = x - c, dy = y - c;
                if (dx * dx + dy * dy <= rad2)
                    st->diskIdx.push_back(y * S + x);
            }

        double sum = 0.0;
        for (int i : st->diskIdx) sum += st->refWork[i];
        double mean = sum / st->diskIdx.size();
        double sq = 0.0;
        for (int i : st->diskIdx) {
            st->refWork[i] -= mean;
            sq += st->refWork[i] * st->refWork[i];
        }
        st->refNorm = std::sqrt(sq);
        m_toolProgress = 0.05;
    });

    for (int chunk = 0; chunk < kChunks; chunk++) {
        int a0 = kAngles * chunk / kChunks;
        int a1 = kAngles * (chunk + 1) / kChunks;
        steps.push_back([this, st, a0, a1, kStepDeg, chunk, kChunks]() {
            const int S = st->S;
            const double c = (S - 1) / 2.0;
            const double outside = st->srcMean;
            for (int a = a0; a < a1; a++) {
                double ang = a * kStepDeg * M_PI / 180.0;
                // Inverse map: a positive angle turns the image clockwise on
                // screen, matching the interactive Rotate tool.
                double ca = std::cos(ang), sa = std::sin(ang);
                double sum = 0.0, sumSq = 0.0, dot = 0.0;
                for (int idx : st->diskIdx) {
                    int x = idx % S, y = idx / S;
                    double dx = x - c, dy = y - c;
                    double sx = c + ca * dx + sa * dy;
                    double sy = c - sa * dx + ca * dy;
                    double v = sampleBilinear(st->srcWork, S, S, sx, sy, outside);
                    sum   += v;
                    sumSq += v * v;
                    dot   += v * st->refWork[idx];
                }
                double n = (double)st->diskIdx.size();
                double var = sumSq - sum * sum / n;
                double denom = (var > 0.0) ? std::sqrt(var) * st->refNorm : 0.0;
                double score = (denom > 0.0) ? dot / denom : 0.0;
                if (score > st->bestScore) {
                    st->bestScore = score;
                    st->bestAngle = a * kStepDeg;
                }
            }
            m_toolProgress = 0.05 + 0.9 * (chunk + 1) / kChunks;
        });
    }

    // Apply the winning angle to the full-resolution image.
    steps.push_back([this, st]() {
        const int w = st->w, h = st->h;
        double ang = st->bestAngle * M_PI / 180.0;
        double ca = std::cos(ang), sa = std::sin(ang);
        double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
        std::vector<double> outPix((size_t)w * h);
        for (int y = 0; y < h; y++) {
            double dy = y - cy;
            for (int x = 0; x < w; x++) {
                double dx = x - cx;
                double sx = cx + ca * dx + sa * dy;
                double sy = cy - sa * dx + ca * dy;
                outPix[(size_t)y * w + x] =
                    sampleBilinear(st->srcFull, w, h, sx, sy, st->srcMean);
            }
        }
        st->srcFull.clear();
        st->srcWork.clear();
        st->refWork.clear();

        finishAlign(st->outIdx, std::move(outPix), w, h,
                    st->pixelSize, st->srcPath);
        m_alignResult = QString("Rotated by %1° (correlation %2)")
                            .arg(st->bestAngle, 0, 'f', 1)
                            .arg(st->bestScore, 0, 'f', 4);
        m_toolProgress = -1;
    });

    chainSteps(std::move(steps));
}
