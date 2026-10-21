#pragma once

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QRect>
#include <QString>

// The keys pressed lately, drawn as chips along the bottom of a frame.
class KeyOverlay : public QObject
{
    Q_OBJECT

public:
    explicit KeyOverlay(QObject *parent = nullptr);

    // How long a key stays on screen.
    static constexpr int lingerMs = 2500;
    static constexpr int maxKeys = 7;

    bool isEmpty() const;
    void paint(QPainter &painter, const QRect &frame) const;
    QStringList labels() const;

public slots:
    void add(const QString &label);
    void clear();

private:
    struct Key
    {
        QString label;
        qint64 at;
        int times;
    };
    void expire() const;

    QElapsedTimer m_clock;
    mutable QList<Key> m_keys;
};
