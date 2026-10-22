// HellForge's network gate in the copied game (net_gate.c). Started from the stand-in's DllMain.
#pragma once

// With HELLFORGE_NETGATE=census and HELLFORGE_NETGATE_LOG set: hooks the process's network calls and logs every attempt.
void NetGateStart(void);
