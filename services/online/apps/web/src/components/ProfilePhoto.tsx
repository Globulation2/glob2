import { MessageError } from '../i18n.tsx';
import { t, useLocale } from '../i18n.tsx';
import { useEffect, useRef, useState } from 'react';
import Cropper, { type Area } from 'react-easy-crop';
import type { SelfAccount } from '@glob2/protocol';
import { api } from '../api.ts';
import { Avatar, ErrorNotice } from './common.tsx';
import 'react-easy-crop/react-easy-crop.css';

/** Reject animated inputs before the browser flattens them for the cropper. */
export function checkPhotoBytes(bytes: ArrayBuffer) {
  const b = new Uint8Array(bytes);
  const view = new DataView(bytes);
  const text = (at: number, n: number) => String.fromCharCode(...b.slice(at, at + n));
  if (b.length > 10 * 1024 * 1024) throw new MessageError('Choose a photo no larger than 10 MiB.');
  if (b[0] === 255 && b[1] === 216 && b[2] === 255) return;
  if (text(1, 3) === 'PNG') {
    for (let i = 8; i + 12 <= b.length;) {
      const len = view.getUint32(i);
      if (text(i + 4, 4) === 'acTL')
        throw new MessageError('Choose a still photo, not an animation.');
      i += 12 + len;
    }
    return;
  }
  if (text(0, 4) === 'RIFF' && text(8, 4) === 'WEBP') {
    for (let i = 12; i + 8 <= b.length;) {
      const len = view.getUint32(i + 4, true);
      if (text(i, 4) === 'ANIM') throw new MessageError('Choose a still photo, not an animation.');
      i += 8 + len + (len % 2);
    }
    return;
  }
  throw new MessageError('Choose a JPEG, PNG or WebP photo.');
}
export async function cropPhoto(file: Blob, area: Area): Promise<Blob> {
  const image = await createImageBitmap(file, { imageOrientation: 'from-image' });
  try {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 512;
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new MessageError('Your browser could not prepare this photo.');
    ctx.drawImage(image, area.x, area.y, area.width, area.height, 0, 0, 512, 512);
    return await new Promise<Blob>((resolve, reject) =>
      canvas.toBlob(
        (b) => (b ? resolve(b) : reject(new MessageError('Could not crop this photo.'))),
        'image/webp',
        0.9,
      ),
    );
  } finally {
    image.close();
  }
}
function PhotoEditor({
  file,
  onClose,
  onSaved,
}: {
  file: File;
  onClose: () => void;
  onSaved: () => void;
}) {
  useLocale();
  const dialog = useRef<HTMLDialogElement>(null);
  const [url, setUrl] = useState('');
  const [crop, setCrop] = useState({ x: 0, y: 0 });
  const [zoom, setZoom] = useState(1);
  const [area, setArea] = useState<Area>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<Error>();
  useEffect(() => {
    const reader = new FileReader();
    reader.onload = () => setUrl(String(reader.result));
    reader.readAsDataURL(file);
    dialog.current?.showModal();
    return () => {
      if (reader.readyState === FileReader.LOADING) reader.abort();
    };
  }, [file]);
  const close = () => {
    dialog.current?.close();
    onClose();
  };
  const save = async () => {
    if (!area) return;
    setBusy(true);
    setError(undefined);
    try {
      await api.uploadAvatar(await cropPhoto(file, area));
      onSaved();
      close();
    } catch (e) {
      setError(e as Error);
      setBusy(false);
    }
  };
  return (
    <dialog
      ref={dialog}
      className="photo-dialog"
      aria-labelledby="photo-title"
      onCancel={(e) => {
        e.preventDefault();
        if (!busy) close();
      }}
    >
      <h2 id="photo-title">{t('Crop your photo')}</h2>
      <p id="photo-instructions" className="caption">
        {t('Drag to position. Use arrow keys to move and the slider to zoom.')}
      </p>
      <div className="photo-crop">
        {url && (
          <Cropper
            cropperProps={{
              role: 'group',
              'aria-label': 'Photo position',
              'aria-describedby': 'photo-instructions',
              'aria-disabled': busy,
            }}
            image={url}
            crop={crop}
            zoom={zoom}
            aspect={1}
            cropShape="round"
            showGrid={false}
            onCropChange={(position) => {
              if (!busy) setCrop(position);
            }}
            onZoomChange={(value) => {
              if (!busy) setZoom(value);
            }}
            onCropComplete={(_, pixels) => setArea(pixels)}
            disableAutomaticStylesInjection
          />
        )}
      </div>
      <label className="field">
        {t('Zoom')}
        <input
          type="range"
          min={1}
          max={3}
          step={0.05}
          value={zoom}
          onChange={(e) => setZoom(Number(e.target.value))}
          disabled={busy}
        />
      </label>
      {error && <ErrorNotice error={error} />}
      <div className="toolbar">
        <button onClick={close} disabled={busy}>
          {t('Cancel')}
        </button>
        <button className="primary" onClick={() => void save()} disabled={busy || !area}>
          {busy ? t('Saving…') : t('Save photo')}
        </button>
      </div>
    </dialog>
  );
}
export function ProfilePhoto({ account, onSaved }: { account: SelfAccount; onSaved: () => void }) {
  useLocale();
  const [file, setFile] = useState<File>();
  const [error, setError] = useState<Error>();
  const [busy, setBusy] = useState(false);
  const [saved, setSaved] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const uploadButton = useRef<HTMLButtonElement>(null);
  const finish = () => {
    setSaved(true);
    onSaved();
  };
  const choose = async (photo: File) => {
    setError(undefined);
    setSaved(false);
    setBusy(true);
    try {
      if (photo.size > 10 * 1024 * 1024)
        throw new MessageError('Choose a photo no larger than 10 MiB.');
      checkPhotoBytes(await photo.arrayBuffer());
      const bitmap = await createImageBitmap(photo, { imageOrientation: 'from-image' });
      const pixels = bitmap.width * bitmap.height;
      bitmap.close();
      if (pixels > 25_000_000)
        throw new MessageError('Choose a photo no larger than 25 megapixels.');
      setFile(photo);
    } catch (e) {
      setError(e instanceof Error ? e : new MessageError('Could not open this photo.'));
    } finally {
      setBusy(false);
    }
  };
  const source = async (value: 'automatic' | 'initials') => {
    setError(undefined);
    setBusy(true);
    setSaved(false);
    try {
      await api.avatarSource(value);
      finish();
    } catch (e) {
      setError(e as Error);
    } finally {
      setBusy(false);
    }
  };
  return (
    <section className="card" aria-labelledby="profile-photo-title">
      <h2 id="profile-photo-title" className="card-title">
        {t('Profile photo')}
      </h2>
      <div className="profile-photo-preview">
        <Avatar account={account} size="large" />
        <div>
          <strong>
            <bdi dir="auto">{account.displayName}</bdi>
          </strong>
          <p className="caption">
            {account.avatarSource === 'uploaded'
              ? t('Your uploaded photo')
              : account.avatarSource === 'initials'
                ? t('Initials only')
                : t('Gravatar when available, otherwise initials')}
          </p>
        </div>
      </div>
      <p>
        {t(
          'Use a photo from your device, or the Gravatar associated with a linked email address. Your photo is public.',
        )}
      </p>
      <input
        ref={input}
        type="file"
        accept="image/jpeg,image/png,image/webp"
        hidden
        aria-label={t('Choose profile photo')}
        onChange={(e) => {
          const next = e.target.files?.[0];
          e.target.value = '';
          if (next) void choose(next);
        }}
      />
      <div className="toolbar">
        <button
          ref={uploadButton}
          className="primary"
          disabled={busy}
          onClick={() => input.current?.click()}
        >
          {t('Upload photo')}
        </button>
        <button disabled={busy} onClick={() => void source('automatic')}>
          {t('Use Gravatar')}
        </button>
        <button disabled={busy} onClick={() => void source('initials')}>
          {t('Use initials')}
        </button>
      </div>
      <p className="caption">
        {t(
          'Still JPEG, PNG or WebP · up to 10 MiB and 25 megapixels. Choosing Gravatar or initials removes your uploaded photo.',
        )}
      </p>
      {error && <ErrorNotice error={error} />}
      <span role="status">{busy ? t('Preparing photo…') : saved ? t('Photo updated.') : ''}</span>
      {file && (
        <PhotoEditor
          file={file}
          onClose={() => {
            setFile(undefined);
            uploadButton.current?.focus();
          }}
          onSaved={finish}
        />
      )}
    </section>
  );
}
