#ifndef MAXIMA_EXPERIMENT_AUDIT_H
#define MAXIMA_EXPERIMENT_AUDIT_H
#include <string>
class Game;
class Order;
namespace MaximaExperimentAudit
{
// Separate JSONL channel. Missing/truncated records are fatal to the consumer.
void open(const std::string& path, const std::string& identities);
void disableOrders(int player);
bool ordersDisabled(int player);
bool enabled();
bool started();
std::string quote(const std::string& text);
void write(const std::string& type, const std::string& fields);
void state(Game& game, const std::string& phase);
void order(Game& game, int player, Order& order, const std::string& phase);
}
#endif
