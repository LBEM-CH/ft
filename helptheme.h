// ---------------------------------------------------------------------------
//  Appearance of the Help dialog
//
//  Two looks, switched by the banner's "Dark mode" button: the manual pages'
//  light palette (the same values as ft-manual's stylesheet, blue banner
//  included) as the default, and the dark scheme the dialog originally had.
//  The font size follows the banner's A+/A- buttons; both choices persist in
//  QSettings across sessions.
//
//  Kept in step with the Help dialog of the 4d application, which embeds ft
//  (HelpTheme in apps/src/widgets/ManualSearchDialog.cpp there), so the two
//  programs answer questions in the same dress.
// ---------------------------------------------------------------------------
#ifndef HELPTHEME_H
#define HELPTHEME_H

#include <QSettings>
#include <QString>

struct HelpTheme {
    bool dark;
    QString windowBg;   // dialog background
    QString banner;     // banner background - the manual pages' blue in light
    QString bannerFg;   // banner text and banner-button text
    QString fg;         // main text
    QString muted;      // secondary text
    QString dim;        // tertiary text (scores, counts)
    QString link;
    QString paneBg;     // results pane and question box background
    QString border;
    QString codeBg;
    QString buttonBg, buttonBorder, buttonFg, buttonCheckedBg;
};

inline HelpTheme helpTheme(bool dark)
{
    if (dark)
        return { true,
                 QStringLiteral("#333333"), QStringLiteral("#23272e"), QStringLiteral("#eeeeee"),
                 QStringLiteral("#eeeeee"), QStringLiteral("#bbbbbb"), QStringLiteral("#999999"),
                 QStringLiteral("#9bbcff"), QStringLiteral("#222222"), QStringLiteral("#888888"),
                 QStringLiteral("#3a3a3a"),
                 QStringLiteral("#888888"), QStringLiteral("#aaaaaa"), QStringLiteral("#eeeeee"),
                 QStringLiteral("#556699") };
    return { false,
             QStringLiteral("#f7f7f8"), QStringLiteral("#2858c4"), QStringLiteral("#ffffff"),
             QStringLiteral("#1d1d20"), QStringLiteral("#5a5a62"), QStringLiteral("#8a8a92"),
             QStringLiteral("#2858c4"), QStringLiteral("#ffffff"), QStringLiteral("#d8d8de"),
             QStringLiteral("#eef0f4"),
             QStringLiteral("#e8eaef"), QStringLiteral("#c0c3cc"), QStringLiteral("#1d1d20"),
             QStringLiteral("#c9d6f2") };
}

inline bool helpDialogDark()
{
    return QSettings("ft", "ft").value("help/dark", false).toBool();
}

inline void setHelpDialogDark(bool dark)
{
    QSettings("ft", "ft").setValue("help/dark", dark);
}

inline int helpDialogFontDelta()
{
    return QSettings("ft", "ft").value("help/fontDelta", 0).toInt();
}

inline void setHelpDialogFontDelta(int delta)
{
    QSettings("ft", "ft").setValue("help/fontDelta", delta);
}

#endif // HELPTHEME_H
