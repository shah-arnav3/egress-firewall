package com.egressfirewall.policy_engine;

import org.springframework.web.bind.annotation.*;
import java.util.List;
import java.util.Map;

@RestController
public class PolicyController {

    // Hardcoded allowlist — the only hosts the agent is permitted to reach.
    // Everything else is denied by default.
    private static final List<String> ALLOWLIST = List.of(
        "example.com",
        "api.anthropic.com",
        "httpbin.org"
    );

    // This is the endpoint the C++ proxy will call before relaying any connection.
    // @PostMapping means it handles HTTP POST requests to /check
    // @RequestBody means Spring automatically parses the JSON body into a Map
    @PostMapping("/check")
    public Map<String, Object> check(@RequestBody Map<String, String> request) {
        String host = request.get("host");
        String port = request.getOrDefault("port", "443");

        System.out.println("Policy check: " + host + ":" + port);

        if (host == null || host.isBlank()) {
            return Map.of("allowed", false, "reason", "Missing host");
        }

        boolean allowed = ALLOWLIST.contains(host.toLowerCase());

        if (allowed) {
            System.out.println("ALLOWED: " + host);
            return Map.of("allowed", true);
        } else {
            System.out.println("DENIED: " + host);
            return Map.of("allowed", false, "reason", "Host not in allowlist");
        }
    }
}