#include "Bus.h"
#include "Node.h"
#include "NodeManager.h"

void Connection::updateNumChannels() {
    if (dir != Direction::output) return;
    int nch = minChannels;
    if (is_connected && output_node >= 0 && output_connection >= 0 && nm) {
        Node* dst = nm->getNode(static_cast<uint16_t>(output_node));
        if (dst) {
            Connection* dstCon = dst->inputs.getConnection(static_cast<uint16_t>(output_connection));
            if (dstCon && dstCon->type == DataType::Waveform && dstCon->numChannels > nch)
                nch = dstCon->numChannels;
        }
    }
    numChannels = nch;
}
