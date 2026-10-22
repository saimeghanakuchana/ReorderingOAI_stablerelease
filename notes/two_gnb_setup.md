# Two-gNB, two-UE setup (one cell per path)

These instructions bring up **two gNBs, each serving one cell on a different carrier**. Each UE attaches to its own gNB, so the two paths no longer share one cell's radio resources.

Everything above the RAN stays as described in [RACK_and_MPTCP_setup.md](RACK_and_MPTCP_setup.md): UE namespaces, GRE tunnels, spraying rules and iperf endpoints. Only the gNB and UE start commands change.

| | gNB1 (path 1) | gNB2 (path 2) |
|---|---|---|
| Config | `etc/gnb_new.conf` | `etc/gnb2.conf` |
| gNB_ID / name | 0xe00 / gNB-Eurecom-5GNRBox | 0xe01 / gNB-path2 |
| nr_cellid / PCI | 12345678 / 0 | 12345679 / 1 |
| Carrier | n78, 106 PRB (40 MHz), center 3619.20 MHz | n78, 51 PRB (20 MHz), center 3648.00 MHz |
| SSB ARFCN / PointA ARFCN | 641280 / 640008 | 643296 / 642588 |
| N2/N3 address | 192.168.70.129 (host address on `demo-oai`) | 192.168.70.190 (IP alias on `demo-oai`) |
| rfsim server port | 4043 | 4044 |
| UE | UE1 (`ue.conf`, IMSI ...031) | UE2 (`ue2.conf`, IMSI ...032) |

**Why these choices:**
- **Same band.** Both cells use n78 with the same numerology and TDD pattern, so the setup carries over to OTA with FR1 radios.
- **Different bandwidths.** 106 vs 51 PRB makes the two paths naturally asymmetric (about 2:1 in capacity).
- **No overlap.** The second carrier sits right above the first, so OTA radios would not interfere.
- **Valid SSB placement.** The SSB of gNB2 lies on the synchronization raster (GSCN-aligned).
- **Checked formulas.** The UE frequency (`-C`) and `--ssb` values were computed with the same formulas that reproduce OAI's CI values for the 24-PRB and 273-PRB configs.

---

## 0. One-time: rebuild with the PDCP fix

`PDCP_reordering/nr_pdcp_entity.c` was fixed on 2026-10-21. With `outOfOrderDelivery`, SDUs were delivered a second time when t-Reordering expired or the entity was re-established. The file is already listed in `file_transfer.sh`, so rebuild once:

```
cd /local/repository
bash file_transfer.sh
bash deploy-oai.sh ran
```

**Toggling outOfOrderDelivery does not need a rebuild.** The flag is read at gNB start from the `pdcp` section of the gNB config:

```
pdcp = {
  drb = {
    t_reordering  = "ms200";
    out_of_order_delivery = 1;   # 1 = enabled, 0 = disabled
  }
};
```

The gNB sends it to the UE in the RRC Reconfiguration (PDCP-Config `outOfOrderDelivery`), and the UE's PDCP picks it up from there. The UE config files are not involved.

To change the flag:
1. Edit the value in **both** `gnb_new.conf` and `gnb2.conf`. Keep the two `pdcp` sections identical.
2. Restart both gNBs.
3. Restart both UEs. The UEs must reattach to receive the new RRC configuration.

Check the gNB log for `CONFIG: out_of_order_delivery = 1`, and the UE log for `add_drb: drb->outOfOrderDelivery ptr = 0x...`. A non-NULL pointer means the flag is enabled.

Note that `t_reordering` is 200 ms in the configs, while the April 27 table lists 50 ms. Set the value you intend to report, and record it per run.

---

## 1. Core network

Unchanged; see [core_ran_setup.md](core_ran_setup.md). Then update `amf_ip_address` in **both** gNB configs to the AMF address:

```
sudo docker inspect oai-amf | grep IPAddress
```

## 2. IP alias for gNB2's N2/N3 address

gNB1 uses the host's address on the CN bridge (192.168.70.129). gNB2 needs its own address, because OAI binds GTP-U (UDP 2152) to the configured N3 address.

First check that .190 is not taken by a container:

```
sudo docker network inspect demo-oai | grep IPv4Address
```

Then add the alias:

```
sudo ip addr add 192.168.70.190/26 dev demo-oai
```

If .190 is taken, pick another free address in the /26 and change both `GNB_IPV4_ADDRESS_*` lines in `gnb2.conf`.

The alias disappears when the docker network is recreated, so re-add it after `docker compose down/up`.

## 3. Start the two gNBs

Pin each process to its own cores. Adjust the core lists to the node.

```
cd /mydata/openairinterface5g/cmake_targets
# gNB1 (path 1)
sudo chrt -f 90 taskset -c 2-5 ./ran_build/build/nr-softmodem -O /local/repository/etc/gnb_new.conf --sa --rfsim
# gNB2 (path 2), in another terminal
sudo chrt -f 90 taskset -c 6-9 ./ran_build/build/nr-softmodem -O /local/repository/etc/gnb2.conf --sa --rfsim
```

Check in the AMF log that **two** gNBs are connected (`docker logs -f oai-amf`). gNB2 should appear as `gNB-path2`.

## 4. UE namespaces

Unchanged: follow steps 1–3 of [RACK_and_MPTCP_setup.md](RACK_and_MPTCP_setup.md) to create `ue1` and `ue2`, the veth pairs, and the `ip_veth_*` variables.

## 5. Start the two UEs

Each UE connects to *its* gNB's rfsim port, with that cell's bandwidth and frequency.

```
cd /mydata/openairinterface5g/cmake_targets
# UE1 -> gNB1 (106 PRB, 3619.20 MHz, port 4043 = default, --ssb 516 = default)
sudo chrt -f 80 taskset -c 10-12 ip netns exec ue1 env ./ran_build/build/nr-uesoftmodem \
  -O /local/repository/etc/ue.conf -r 106 --numerology 1 --band 78 -C 3619200000 \
  --rfsim --rfsimulator.serveraddr $ip_veth_main1 --rfsimulator.serverport 4043

# UE2 -> gNB2 (51 PRB, 3648.00 MHz, port 4044, --ssb 234)
sudo chrt -f 81 taskset -c 13-15 ip netns exec ue2 env ./ran_build/build/nr-uesoftmodem \
  -O /local/repository/etc/ue2.conf -r 51 --numerology 1 --band 78 -C 3648000000 --ssb 234 \
  --rfsim --rfsimulator.serveraddr $ip_veth_main2 --rfsimulator.serverport 4044
```

- **Channel model.** For good channel conditions, comment out `@include "channelmod_rfsimu.conf"` in the UE config files, as before. To use the channel model, add `--rfsimulator.options chanmod`.
- **Two-node topology.** In that setup, `--rfsimulator.serveraddr` is the gNB node's address instead of `$ip_veth_main*`, as in the existing notes.

## 6. Verify before running experiments

1. **Both UEs are attached.** Each UE has an `oaitun_ue1` address in its own namespace:
   ```
   sudo ip netns exec ue1 ip -o -4 addr show dev oaitun_ue1
   sudo ip netns exec ue2 ip -o -4 addr show dev oaitun_ue1
   ```
2. **Each UE is on the right cell.** Check the gNB logs: UE1's RNTI should appear in gNB1's MAC stats, and UE2's RNTI in gNB2's.
3. **Single-path capacity per path.** Run iperf over each path alone, as in the "single paths" block of RACK_and_MPTCP_setup.md. Path 2 should be roughly half of path 1 (51 vs 106 PRB). Both run at the same time should now add up, since they no longer share a cell.
4. **Idle RTT per path.** Run `ping -c 50` per path. Check whether the roughly 800 ms RTT mode seen in the April 27 runs is still present.

Then continue with GRE tunnels, routing, spraying, and the RACK and MPTCP runs exactly as in [RACK_and_MPTCP_setup.md](RACK_and_MPTCP_setup.md), steps 5 onward.

---

## Troubleshooting

- **gNB2 fails to start with a bind error on 2152 or SCTP.** The alias is missing or the address is wrong (step 2).
- **UE2 does not find the cell.** Check that `-r 51 -C 3648000000 --ssb 234` and `--rfsimulator.serverport 4044` are all set. A wrong `--ssb` value makes the UE miss the SSB.
- **gNB2 asserts on the BWP or CORESET.** The 51-PRB values (`initialDLBWPlocationAndBandwidth = 13750`, CORESET0 index 12) come from OAI's CI config `gnb.sa.band78.51prb.usrpb200.conf`. If an assertion still fires, compare the `servingCellConfigCommon` block against that file.
- **Both UEs attach to the same gNB.** Each UE must point at its own rfsim port (4043 / 4044).
