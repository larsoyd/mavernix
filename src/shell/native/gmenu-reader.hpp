#pragma once

#include <QObject>
#include <QVariantList>
#include <memory>

class GMenuReader : public QObject
{
    Q_OBJECT

  public:
    explicit GMenuReader(QObject* parent = nullptr);
    ~GMenuReader() override;
    void follow(const QVariantMap& address);
    QVariantList items() const;
    void activate(int id);
    void aboutToShow(int id);

  signals:
    void itemsChanged();

  private:
    struct State;
    std::unique_ptr<State> state;
};
