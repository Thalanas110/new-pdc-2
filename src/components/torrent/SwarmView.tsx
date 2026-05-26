import type { SwarmPeerEntry } from '../../lib/transferModel';

type SwarmViewProps = {
  peerHost: string;
  peerPort: number;
  peers: SwarmPeerEntry[];
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
  onBootstrap: () => void;
};

export function SwarmView({
  peerHost,
  peerPort,
  peers,
  onPeerHostChange,
  onPeerPortChange,
  onBootstrap,
}: SwarmViewProps) {
  return (
    <section className="loop-page" aria-labelledby="swarm-heading">
      <h1 id="swarm-heading">Swarm</h1>
      <input aria-label="Peer host" value={peerHost} onChange={(event) => onPeerHostChange(event.target.value)} />
      <input
        aria-label="Peer port"
        type="number"
        value={peerPort}
        onChange={(event) => onPeerPortChange(Number(event.target.value))}
      />
      <button type="button" onClick={onBootstrap}>
        Bootstrap peer
      </button>
      <p>Peers {peers.length}</p>
    </section>
  );
}
