# 🔒 Security Policy — Jirnyak/starcluster

> **Vulnerability Reporting, Threat Model & Responsible Disclosure**  
> Maintained by the **Жирняк & Адольф Петушков** Engineering Syndicate

---

## 🛡️ 1. Supported Versions

We actively maintain and provide security patches for the following versions of **Jirnyak/starcluster**:

| Version | Supported | Status |
| :--- | :--- | :--- |
| `main` branch | ✅ Yes | Actively patched |
| Latest Release tag | ✅ Yes | Critical vulnerability fixes |
| Historical branches | ❌ No | Please upgrade to `main` |

---

## 🎯 2. Domain Threat Model

Security audits for **Jirnyak/starcluster** focus on the following attack surfaces and failure modes:

1. **Numerical Divergence**:  Close gravitational encounters causing infinite acceleration and NaN orbital vectors.
2. **Save State Manipulation**:  Tampering with local storage JSON to inject arbitrary credits or ship properties.
3. **Main Thread Lock**:  Running O(N^2) direct gravitational summation instead of octree approximations.

---

## 🚨 3. Reporting a Vulnerability

If you discover a security vulnerability or exploit in **Jirnyak/starcluster**, please report it responsibly:

### 3.1 Submission Procedure
1. **Do NOT open a public GitHub issue** with exploitable proofs-of-concept.
2. Open a **Private Security Advisory** on GitHub:  
   Navigate to `Security` -> `Advisories` -> `Report a vulnerability`.
3. Alternatively, contact the maintainers directly through authenticated syndicate communication channels.

### 3.2 Report Details
To help us investigate and patch the vulnerability quickly, please include:
* **Vulnerability Description**: Detailed explanation of the flaw and affected subsystem.
* **Proof-of-Concept (PoC)**: Minimal, reproducible steps or scripts demonstrating the vulnerability.
* **Impact Assessment**: Potential consequences (e.g. arbitrary code execution, denial of service, data exfiltration).
* **Proposed Mitigation**: If you have an architectural patch or suggested fix, please share it.

---

## ⏱️ 4. Response & Disclosure Timeline

1. **Acknowledgment**: We acknowledge receipt of your vulnerability report within **48 hours**.
2. **Triage & Reproduction**: We evaluate the severity and reproduce the issue within **5 business days**.
3. **Patch & Verification**: A secure patch is developed, tested against the test suite, and committed to a private branch.
4. **Coordinated Disclosure**: Once the fix is published and verified on `main`, a CVE / advisory will be published crediting the researcher.

---

### 👥 Engineering Syndicate
Maintained by **Жирняк** & **Адольф Петушков**.
