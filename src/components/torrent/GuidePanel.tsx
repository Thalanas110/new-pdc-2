type GuidePanelProps = {
  transferPort: number;
};

export function GuidePanel({ transferPort }: GuidePanelProps) {
  return (
    <section className="rail-card" aria-labelledby="guide-heading">
      <div className="rail-card__eyebrow">User Guide</div>
      <h2 id="guide-heading">How to operate the swarm.</h2>
      <ol className="guide-steps">
        <li>Connect every laptop to the same phone hotspot and launch Loopline on each machine.</li>
        <li>
          On the first laptop, open <strong>Publish</strong>, choose one file, and press <strong>Publish file</strong>.
        </li>
        <li>
          If another laptop does not see the file, open <strong>Swarm</strong>, enter the seeder&apos;s hotspot IP and
          port {transferPort}, then press <strong>Bootstrap peer</strong>.
        </li>
        <li>
          Open <strong>Library</strong>, press <strong>Download</strong>, then switch to <strong>Downloads</strong> to
          watch verified pieces accumulate and the downloader become a seeder.
        </li>
      </ol>
      <p className="guide-note">
        Demo tip: after one download finishes, start the same file on a third laptop to prove reseeding and multi-peer
        distribution.
      </p>
    </section>
  );
}
