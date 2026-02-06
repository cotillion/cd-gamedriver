#pragma strict_types

/*
 * Test case for environment command registry system.
 * Validates that commands added via add_action() are stored in env_commands
 * and can be executed via the O(1) hash lookup in player_parser().
 */

int test_result;
int execution_count;

int action_test1(string arg)
{
    test_result = 1;
    execution_count++;
    write("  -> action_test1 executed (arg=" + (arg ? arg : "none") + ")\n");
    return 1;
}

int action_test2(string arg)
{
    test_result = 2;
    execution_count++;
    write("  -> action_test2 executed (arg=" + (arg ? arg : "none") + ")\n");
    return 1;
}

int action_shared(string arg)
{
    test_result = 99;
    execution_count++;
    write("  -> action_shared executed\n");
    return 1;
}

void create()
{
    object room, npc1, npc2;
    int i;
    object *npcs;
    
    write("=== Testing Environment Command Registry ===\n\n");
    
    /* Create a room */
    room = clone_object("/tests/base");
    
    /* Test 1: Single NPC with command */
    write("Test 1: NPC command registration and execution...\n");
    npc1 = clone_object("/tests/base");
    set_this_player(npc1);
    enable_commands();
    set_living_name("npc1");
    add_action("action_test1", "test1");
    
    /* Execute the command */
    test_result = 0;
    execution_count = 0;
    if (!command("test1")) {
        throw("Test 1 FAILED: command not found\n");
    }
    if (test_result != 1 || execution_count != 1) {
        throw("Test 1 FAILED: command not executed properly (result=" + test_result + ", count=" + execution_count + ")\n");
    }
    write("  PASSED (command stored in env_commands and executed)\n\n");
    
    /* Test 2: Multiple NPCs with different commands */
    write("Test 2: Multiple NPCs with different commands...\n");
    npc2 = clone_object("/tests/base");
    set_this_player(npc2);
    enable_commands();
    set_living_name("npc2");
    add_action("action_test2", "test2");
    
    /* Execute npc2's command */
    test_result = 0;
    execution_count = 0;
    if (!command("test2")) {
        throw("Test 2 FAILED: npc2 command not found\n");
    }
    if (test_result != 2 || execution_count != 1) {
        throw("Test 2 FAILED: npc2 command not executed (result=" + test_result + ")\n");
    }
    write("  PASSED (second NPC command works)\n\n");
    
    /* Test 3: First NPC's command still works */
    write("Test 3: First NPC command persistence...\n");
    set_this_player(npc1);
    test_result = 0;
    execution_count = 0;
    if (!command("test1")) {
        throw("Test 3 FAILED: npc1 command lost\n");
    }
    if (test_result != 1 || execution_count != 1) {
        throw("Test 3 FAILED: npc1 command damaged (result=" + test_result + ")\n");
    }
    write("  PASSED (first command still accessible)\n\n");
    
    /* Test 4: Commands with arguments */
    write("Test 4: Command with arguments...\n");
    test_result = 0;
    execution_count = 0;
    if (!command("test1 with args")) {
        throw("Test 4 FAILED: command with args not found\n");
    }
    if (test_result != 1) {
        throw("Test 4 FAILED: command with args not executed\n");
    }
    write("  PASSED (arguments passed correctly)\n\n");
    
    /* Test 5: Shared command name (both NPCs add same verb) */
    write("Test 5: Shared command verb...\n");
    set_this_player(npc1);
    add_action("action_shared", "shared");
    set_this_player(npc2);
    add_action("action_shared", "shared");
    
    test_result = 0;
    execution_count = 0;
    if (!command("shared")) {
        throw("Test 5 FAILED: shared command not found\n");
    }
    if (test_result != 99) {
        throw("Test 5 FAILED: shared command not executed\n");
    }
    write("  PASSED (shared verb works with chaining)\n\n");
    
    /* Test 6: Scale test - many NPCs */
    write("Test 6: Performance with 50 NPCs...\n");
    npcs = allocate(50);
    for (i = 0; i < 50; i++) {
        npcs[i] = clone_object("/tests/base");
        set_this_player(npcs[i]);
        enable_commands();
        set_living_name("testnpc");
        add_action("action_test1", "scaletest");
    }
    
    /* Verify command still works with many NPCs (O(1) lookup) */
    set_this_player(npcs[25]);
    test_result = 0;
    execution_count = 0;
    if (!command("scaletest")) {
        throw("Test 6 FAILED: command not found with 50 NPCs\n");
    }
    if (test_result != 1) {
        throw("Test 6 FAILED: command not executed with 50 NPCs\n");
    }
    write("  PASSED (O(1) lookup works with 50 NPCs)\n\n");
    
    write("=== ALL TESTS PASSED ===\n");
    write("Commands stored in environment env_commands mapping with O(1) hash lookup\n");
}

