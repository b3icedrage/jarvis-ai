import React from 'react';
import Header from './Header';
import Stories from './Stories';
import Feed from './Feed';
import './App.css';

const App: React.FC = () => {
  return (
    <div className="app">
      <Header />
      <Stories />
      <Feed />
    </div>
  );
};

export default App;
