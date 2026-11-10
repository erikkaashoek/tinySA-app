#pragma once
// Big-digit number editor, as on a bench signal generator: the wheel or Up/Down over a
// digit steps that digit (with carry, clamped to the range), Left/Right select a digit,
// typing or a double click opens inline entry (Enter applies, Esc cancels).
#include <QWidget>
#include <functional>
#include <optional>

class QLineEdit;

class DigitEdit : public QWidget {
    Q_OBJECT
public:
    explicit DigitEdit(QWidget *parent = nullptr);

    // Shows value / scale with `decimals` decimals, e.g. Hz as MHz: scale 1e6, decimals 6.
    // group: a gap between groups of three decimals (433.920 000); leadingZeros: dimmed
    // zeros up to the range's largest digit (so it can be tuned), else blank.
    void setFormat(double scale, int decimals, const QString &unit, bool group, bool leadingZeros);
    void setRange(double min, double max);
    void setParser(std::function<std::optional<double>(const QString &)> parse);  // typed text -> value
    void setValue(double v);        // no signal
    double value() const { return m_value; }
    void setUnknown(bool unknown);  // shows "?" until the user sets a value
    bool isUnknown() const { return m_unknown; }
    QString text() const;           // as shown, without gaps: "433.920000", "-30.0", "?"
    bool enter(const QString &typed);  // as typed entry; false if it does not parse
    bool isEditing() const;         // inline entry open

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueEdited(double v);  // user changes only

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Cell {
        QString text;
        int digit = -1;  // power of ten in units of the last decimal; -1 = sign, point, gap
        bool dim = false, blank = false;
        QRectF rect;
    };
    QVector<Cell> cells(QFont *digitFont = nullptr) const;  // laid out in the current size
    int intDigits() const;
    qint64 units() const;  // value in units of the last decimal
    int digitAt(QPointF p) const;
    void step(int digit, int count);
    void startEntry(const QString &first);
    void finishEntry(bool apply);

    double m_value = 0, m_min = 0, m_max = 1, m_scale = 1;
    int m_decimals = 0;
    QString m_unit;
    bool m_group = false, m_leadingZeros = false, m_unknown = false;
    int m_hover = -1, m_selected = 0;
    double m_wheel = 0;  // partial wheel steps (touchpads)
    std::function<std::optional<double>(const QString &)> m_parse;
    QLineEdit *m_entry;
};
