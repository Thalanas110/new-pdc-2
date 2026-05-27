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
    <section className="view-shell" aria-labelledby="swarm-heading">
      <header className="view-header">
        <div>
          <p className="view-eyebrow">Peer Discovery</p>
          <h1 id="swarm-heading">Swarm</h1>
        </div>
        <p className="view-summary">
          Use one bootstrap peer when the phone hotspot refuses to broadcast, then let peer gossip do the rest.
        </p>
      </header>

      <div className="swarm-grid">
        <section className="data-card" aria-labelledby="bootstrap-heading">
          <div className="data-card__eyebrow">Bootstrap fallback</div>
          <h2 id="bootstrap-heading">Join the active swarm.</h2>
          <div className="field-stack">
            <label className="field-group">
              <span>Peer host</span>
              <input aria-label="Peer host" value={peerHost} onChange={(event) => onPeerHostChange(event.target.value)} />
            </label>
            <label className="field-group">
              <span>Peer port</span>
              <input
                aria-label="Peer port"
                type="number"
                value={peerPort}
                onChange={(event) => onPeerPortChange(Number(event.target.value))}
              />
            </label>
          </div>
          <p className="field-note">
            Same hotspot only. Use the IP of a laptop that is already seeding or has the file in its library.
          </p>
          <button type="button" className="action-button action-button--dark" onClick={onBootstrap}>
            Bootstrap peer
          </button>
        </section>

        <section className="data-card data-card--tall" aria-labelledby="peer-roster-heading">
          <div className="data-card__eyebrow">Known nodes</div>
          <h2 id="peer-roster-heading">Peer roster</h2>
          {peers.length === 0 ? (
            <div className="empty-panel">
              <strong>No peers tracked yet.</strong>
              <p>Bootstrap one laptop first, then return here to confirm the swarm is visible.</p>
            </div>
          ) : (
            <ul className="peer-list">
              {peers.map((peer) => (
                <li key={`${peer.host}:${peer.port}`} className="peer-card">
                  <div className="peer-card__row">
                    <strong>{peer.nodeId || `${peer.host}:${peer.port}`}</strong>
                    <span className={`peer-state ${peer.reachable ? 'live' : 'cold'}`}>
                      {peer.reachable ? 'Reachable' : 'Unreachable'}
                    </span>
                  </div>
                  <p>{peer.host}:{peer.port}</p>
                  <div className="meta-strip">
                    <span>{peer.source}</span>
                    <span>{peer.lastSeenAt || 'Seen just now'}</span>
                  </div>
                </li>
              ))}
            </ul>
          )}
        </section>
      </div>
    </section>
  );
}
