import { createFileRoute } from '@tanstack/react-router';
import { HomeView } from '../../components/LooplineTransferApp';

export const Route = createFileRoute('/_public/')({
  component: HomeView,
});
