#pragma once

#include <QObject>
#include <QVariantMap>
#include <memory>

class XMenuProperties : public QObject
{
    Q_OBJECT

  public:
    explicit XMenuProperties(QObject* parent = nullptr);
    ~XMenuProperties() override;
    void request(uint window);

  signals:
    void resolved(uint window, const QVariantMap& address);

  private:
    struct State;
    std::unique_ptr<State> state;
};
