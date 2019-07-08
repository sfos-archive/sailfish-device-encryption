/*
 * Copyright (c) 2019 Jolla Ltd.
 */

#include "agent.h"

using namespace Sailfish;

int main(int argc, char **argv)
{
    (void)argv;

    /* FIXME: Ensure that we do not get false positive systemd triggers.
     *
     * Meanwhile: Check that triggers we do get are something we ought
     * to handle in the context of sailfish-unlock-ui i.e. lipstick has not
     * been started yet. Return success so that systemd does not need
     * to consider restarting the oneshot.
     */
    if (!Agent::checkIfAgentCanRun())
        return EXIT_SUCCESS;

    Agent agent;
    /* If argc != 1, enable UI debugging */
    return agent.execute(argc != 1);
}
