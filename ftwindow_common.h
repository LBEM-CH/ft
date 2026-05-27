// Common includes shared by all ftwindow_*.cpp files
#ifndef FTWINDOW_COMMON_H
#define FTWINDOW_COMMON_H

#include "ftwindow.h"
#include "mrcloader.h"
#include "fft.h"

#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QInputDialog>
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QBuffer>
#include <QSettings>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QNativeGestureEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QFont>
#include <QFontMetrics>
#include <QShortcut>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <random>
#if FT_HAVE_THREADS
#include <thread>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif // FTWINDOW_COMMON_H
