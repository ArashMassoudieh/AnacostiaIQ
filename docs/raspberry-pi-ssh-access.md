# Connect to a new Raspberry Pi over SSH

Use this guide when bringing a new AnacostiaIQ station online, including a Pi
whose hostname and IP address have not been recorded. You need both a network
route to the Pi and the username plus password or SSH key configured on it.
Raspberry Pi OS has no universal default login.

## 1. Prepare a new microSD card (preferred)

In Raspberry Pi Imager, select the Pi model, Raspberry Pi OS, and the correct
microSD card. Before writing, use the OS customization settings to:

1. Choose a unique hostname (for example, `pangborn-pi`) and a username.
2. Set a password or add your SSH public key; enable the SSH service.
3. Configure Wi-Fi SSID, password, and country if using Wi-Fi. Ethernet needs
   no Wi-Fi credentials.
4. Record the hostname, username, and authentication method in the station's
   private deployment notes. Do not commit passwords or private keys.

Write the card, connect the Pi to the intended network, and power it on.
Wait a few minutes for its first boot. From a computer on the same local
network, try:

```bash
ssh YOUR_USERNAME@pangborn-pi.local
```

The `.local` name uses local multicast DNS (mDNS/Avahi). It may fail on a
campus, guest, or isolated network even when the Pi is online. If it fails,
continue below.

## 2. Discover an already running Pi with an unknown address

First check that the Pi is powered on and physically connected or has the
correct Wi-Fi credentials. Connect your computer to the same network, if
possible. Find the username and authentication method used when its card was
prepared.

### Check the router or network dashboard

Look at the router's connected devices / DHCP leases. Power the Pi off, check
the list, power it back on, and look for the new entry. Note its hostname and
IP address. On a managed network, ask the network administrator if you cannot
view DHCP leases.

### Try local discovery

If the default hostname was used and there is only one such Pi on the network:

```bash
ping -c 2 raspberrypi.local
ssh YOUR_USERNAME@raspberrypi.local
```

Do not assume the response is your Pi if multiple Pis use the same hostname.

### Scan your own local subnet from Linux

Find your active network interface and its address:

```bash
ip -br addr
ip route
```

On a network you administer or are authorized to inspect, install and run
`arp-scan`:

```bash
sudo apt update
sudo apt install arp-scan
sudo arp-scan --interface=YOUR_INTERFACE --localnet
```

Run the scan before and after booting the Pi and compare the results. Identify
a likely new address, then try SSH with the **known username**:

```bash
ssh -o ConnectTimeout=5 YOUR_USERNAME@CANDIDATE_IP
```

ARP scanning only sees devices on the local broadcast domain. A different
VLAN/subnet or client isolation can prevent discovery. If you cannot reach
the Pi on a managed network, ask for its DHCP lease or an approved access path.

## 3. Verify the machine after login

```bash
hostname
whoami
hostname -I
cat /proc/device-tree/model
```

Check the hostname, username, address, and Pi model before changing any
station configuration. Record the correct hostname and username. The IP
address may later change.

If the hostname needs changing:

```bash
sudo hostnamectl set-hostname pangborn-pi
sudo reboot
```

Reconnect with `ssh YOUR_USERNAME@pangborn-pi.local` if mDNS works there.

## 4. Troubleshoot failed SSH connections

| Result | What to check |
|---|---|
| Name does not resolve | Use DHCP leases or local discovery; `.local` may be blocked or unavailable. |
| Connection times out | Verify power, network, address, subnet/VLAN access, and any client isolation or firewall. |
| Connection refused | The Pi is reachable but SSH may be disabled. Enable it using local console access or prepare the card again. |
| Permission denied | Check the exact username, password, authorized public key, and selected SSH key. |
| Host key has changed | Verify the Pi's identity first. Only then remove the stale entry with `ssh-keygen -R HOST_OR_IP` and reconnect. |

With a monitor and keyboard attached to the Pi, log in locally and check:

```bash
hostname
hostname -I
sudo systemctl enable --now ssh
```

If you cannot authenticate locally either, consider preparing a new card
with known credentials. **Reimaging erases data on the card**; preserve any
needed station configuration and queued measurements first.

## 5. Remote access after initial setup

For access from outside the local network, configure an approved VPN such as
Tailscale on both the Pi and your computer. Inspect `tailscale status` for
the Pi's current name or Tailscale address, then use
`ssh YOUR_USERNAME@TAILSCALE_NAME_OR_IP`. A `.local` name ordinarily
works only on the local network.

For existing CUA stations, the lab Pi has used
`ssh behzad@behzad-twin-pi-lab.local`; the field Pi has used
`ssh sean@100.90.185.42` over Tailscale. Verify current network status
before relying on any previously recorded address.
