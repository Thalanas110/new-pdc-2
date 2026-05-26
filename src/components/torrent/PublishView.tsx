import type { ChangeEvent } from 'react';

type PublishViewProps = {
  selectedFile: File | null;
  publishing: boolean;
  publishPercent: number;
  onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
  onPublish: () => void;
};

export function PublishView({
  selectedFile,
  publishing,
  publishPercent,
  onFileChange,
  onPublish,
}: PublishViewProps) {
  return (
    <section className="loop-page" aria-labelledby="publish-heading">
      <h1 id="publish-heading">Publish</h1>
      <input aria-label="Publish file" type="file" onChange={onFileChange} />
      <button type="button" onClick={onPublish} disabled={!selectedFile || publishing}>
        Publish file
      </button>
      <p>{selectedFile ? selectedFile.name : 'No file selected'}</p>
      <p>Progress {publishPercent}%</p>
    </section>
  );
}
